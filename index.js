// ***************************************************************************
// Copyright (c) 2021 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************

'use strict'

const { BridgeClient } = require('./bridge/client')

function shouldUseBridge() {
  return process.env.SQLANYWHERE_BRIDGE === '1'
}

const bridgeEnabled = shouldUseBridge()
const native = bridgeEnabled ? null : require('./native')
let bridgeDebugLoggingEnabled = false
const activeBridgeClients = new Set()

function ensureCallback(callback, methodName) {
  if (typeof callback !== 'function') {
    throw new TypeError(`${methodName} callback is required`)
  }
}

function invokeWithCallback(methodName, callback, operation) {
  ensureCallback(callback, methodName)
  Promise.resolve()
    .then(operation)
    .then((result) => callback(null, result))
    .catch((error) => callback(error))
}

class BridgeStatement {
  constructor(client, statementId) {
    this.client = client
    this.statementId = statementId
  }

  exec(paramsOrCallback, maybeCallback) {
    const callback = typeof paramsOrCallback === 'function' ? paramsOrCallback : maybeCallback
    const params = typeof paramsOrCallback === 'function' ? undefined : paramsOrCallback
    invokeWithCallback('exec', callback, () =>
      this.client.request('stmtExec', {
        statementId: this.statementId,
        params,
      }))
  }

  getMoreResults(callback) {
    invokeWithCallback('getMoreResults', callback, () =>
      this.client.request('stmtGetMoreResults', {
        statementId: this.statementId,
      }))
  }

  drop(callback) {
    invokeWithCallback('drop', callback, () =>
      this.client.request('stmtDrop', {
        statementId: this.statementId,
      }))
  }
}

class Connection {
  constructor() {
    this._client = null
    this._connected = false
  }

  _ensureClient() {
    if (!this._client) {
      this._client = new BridgeClient()
      activeBridgeClients.add(this._client)
      if (bridgeDebugLoggingEnabled) {
        this._client.request('debugLogging', { enable: true }).catch(() => {})
      }
    }
    return this._client
  }

  _requireConnectedClient() {
    if (!this._client || !this._connected) {
      throw new Error('Connection is not established')
    }
    return this._client
  }

  _closeClient() {
    if (!this._client) {
      return
    }
    activeBridgeClients.delete(this._client)
    this._client.close()
    this._client = null
  }

  connect(connectionParams, callback) {
    invokeWithCallback('connect', callback, async () => {
      const client = this._ensureClient()
      try {
        await client.request('connect', { connectionParams })
        this._connected = true
      } catch (error) {
        this._closeClient()
        throw error
      }
    })
  }

  disconnect(callback) {
    invokeWithCallback('disconnect', callback, async () => {
      if (!this._client) {
        this._connected = false
        return
      }
      try {
        await this._client.request('disconnect')
      } finally {
        this._connected = false
        this._closeClient()
      }
    })
  }

  close(callback) {
    this.disconnect(callback)
  }

  exec(sql, paramsOrCallback, maybeCallback) {
    const callback = typeof paramsOrCallback === 'function' ? paramsOrCallback : maybeCallback
    const params = typeof paramsOrCallback === 'function' ? undefined : paramsOrCallback
    invokeWithCallback('exec', callback, () =>
      this._requireConnectedClient().request('exec', {
        sql,
        params,
      }))
  }

  prepare(sql, callback) {
    invokeWithCallback('prepare', callback, async () => {
      const client = this._requireConnectedClient()
      const result = await client.request('prepare', { sql })
      return new BridgeStatement(client, result.statementId)
    })
  }

  commit(callback) {
    invokeWithCallback('commit', callback, () => this._requireConnectedClient().request('commit'))
  }

  rollback(callback) {
    invokeWithCallback('rollback', callback, () => this._requireConnectedClient().request('rollback'))
  }

  connected() {
    return this._connected
  }
}

class createConnection extends Connection {}

function debugLogging(enable) {
  if (bridgeEnabled) {
    bridgeDebugLoggingEnabled = Boolean(enable)
    for (const client of activeBridgeClients) {
      client.request('debugLogging', { enable: bridgeDebugLoggingEnabled }).catch(() => {})
    }
    return
  }
  native.debugLogging(enable)
}

if (bridgeEnabled) {
  module.exports = {
    Connection,
    createConnection,
    debugLogging,
  }
} else {
  module.exports = native
}
