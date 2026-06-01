'use strict'

const path = require('path')
const { spawn } = require('child_process')
const { serializeLine, parseLine } = require('./protocol')

function normalizeError(payload) {
  if (!payload) {
    return new Error('Bridge request failed')
  }
  const error = new Error(payload.message || 'Bridge request failed')
  if (payload.stack) {
    error.stack = payload.stack
  }
  return error
}

function parseJsonArrayEnv(value) {
  if (!value) {
    return null
  }
  const parsed = JSON.parse(value)
  if (!Array.isArray(parsed) || !parsed.every((entry) => typeof entry === 'string')) {
    throw new Error('SQLANYWHERE_BRIDGE_ARGS must be a JSON string array')
  }
  return parsed
}

function resolveBridgeCommand() {
  const executable = process.env.SQLANYWHERE_BRIDGE_EXECUTABLE
  const customServerScript = process.env.SQLANYWHERE_BRIDGE_SERVER
  const defaultServerScript = path.join(__dirname, 'server.js')
  const serverScript = customServerScript || defaultServerScript

  if (executable) {
    return {
      command: executable,
      args: parseJsonArrayEnv(process.env.SQLANYWHERE_BRIDGE_ARGS) || [serverScript],
    }
  }

  if (process.platform === 'darwin' && process.arch === 'arm64') {
    return {
      command: 'arch',
      args: ['-x86_64', process.execPath, serverScript],
    }
  }

  return {
    command: process.execPath,
    args: [serverScript],
  }
}

class BridgeClient {
  constructor() {
    const { command, args } = resolveBridgeCommand()
    this.child = spawn(command, args, {
      stdio: ['pipe', 'pipe', 'pipe'],
      env: process.env,
    })
    this.nextRequestId = 1
    this.pending = new Map()
    this.buffer = ''
    this.exited = false

    this.child.stdout.setEncoding('utf8')
    this.child.stdout.on('data', (chunk) => {
      this.buffer += chunk
      let newlineIndex = this.buffer.indexOf('\n')
      while (newlineIndex !== -1) {
        const line = this.buffer.slice(0, newlineIndex)
        this.buffer = this.buffer.slice(newlineIndex + 1)
        this.onLine(line)
        newlineIndex = this.buffer.indexOf('\n')
      }
    })

    this.child.on('error', (error) => {
      this.failPending(error)
    })

    this.child.on('exit', (code, signal) => {
      this.exited = true
      this.failPending(new Error(`Bridge process exited (code=${code}, signal=${signal || 'none'})`))
    })
  }

  onLine(line) {
    if (!line.trim()) {
      return
    }

    let payload
    try {
      payload = parseLine(line)
    } catch (error) {
      this.failPending(error)
      return
    }

    if (!payload || payload.id == null) {
      return
    }

    const pending = this.pending.get(payload.id)
    if (!pending) {
      return
    }
    this.pending.delete(payload.id)

    if (payload.ok) {
      pending.resolve(payload.result)
      return
    }
    pending.reject(normalizeError(payload.error))
  }

  failPending(error) {
    for (const { reject } of this.pending.values()) {
      reject(error)
    }
    this.pending.clear()
  }

  request(method, params) {
    if (this.exited) {
      return Promise.reject(new Error('Bridge process is not running'))
    }

    const id = this.nextRequestId++
    const payload = {
      id,
      method,
      params,
    }

    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject })
      this.child.stdin.write(serializeLine(payload), (error) => {
        if (!error) {
          return
        }
        this.pending.delete(id)
        reject(error)
      })
    })
  }

  close() {
    if (!this.exited) {
      this.child.kill()
    }
  }
}

module.exports = {
  BridgeClient,
}
