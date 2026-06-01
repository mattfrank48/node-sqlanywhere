'use strict'

const sqlanywhere = require('../native')
const { serializeLine, parseLine } = require('./protocol')

function call(method, ...args) {
  return new Promise((resolve, reject) => {
    method(...args, (error, result) => {
      if (error) {
        reject(error)
        return
      }
      resolve(result)
    })
  })
}

function toErrorPayload(error) {
  return {
    message: error && error.message ? error.message : String(error),
    stack: error && error.stack ? error.stack : undefined,
  }
}

const connection = new sqlanywhere.Connection()
const statements = new Map()
let nextStatementId = 1
let inputBuffer = ''

async function handleRequest(request) {
  const { method, params = {} } = request

  switch (method) {
    case 'connect':
      await call(connection.connect.bind(connection), params.connectionParams)
      return null
    case 'disconnect':
      await call(connection.disconnect.bind(connection))
      return null
    case 'exec':
      return call(connection.exec.bind(connection), params.sql, params.params)
    case 'prepare': {
      const statement = await call(connection.prepare.bind(connection), params.sql)
      const statementId = String(nextStatementId++)
      statements.set(statementId, statement)
      return { statementId }
    }
    case 'commit':
      await call(connection.commit.bind(connection))
      return null
    case 'rollback':
      await call(connection.rollback.bind(connection))
      return null
    case 'stmtExec': {
      const statement = statements.get(params.statementId)
      if (!statement) {
        throw new Error('Unknown statement id')
      }
      return call(statement.exec.bind(statement), params.params)
    }
    case 'stmtGetMoreResults': {
      const statement = statements.get(params.statementId)
      if (!statement) {
        throw new Error('Unknown statement id')
      }
      return call(statement.getMoreResults.bind(statement))
    }
    case 'stmtDrop': {
      const statement = statements.get(params.statementId)
      if (!statement) {
        return null
      }
      await call(statement.drop.bind(statement))
      statements.delete(params.statementId)
      return null
    }
    case 'connected':
      return connection.connected()
    case 'debugLogging':
      sqlanywhere.debugLogging(Boolean(params.enable))
      return null
    default:
      throw new Error(`Unknown bridge method: ${method}`)
  }
}

function writeResponse(payload) {
  process.stdout.write(serializeLine(payload))
}

async function handleLine(line) {
  if (!line.trim()) {
    return
  }

  let request
  try {
    request = parseLine(line)
  } catch (error) {
    writeResponse({
      id: null,
      ok: false,
      error: toErrorPayload(error),
    })
    return
  }

  try {
    const result = await handleRequest(request)
    writeResponse({
      id: request.id,
      ok: true,
      result,
    })
  } catch (error) {
    writeResponse({
      id: request.id,
      ok: false,
      error: toErrorPayload(error),
    })
  }
}

process.stdin.setEncoding('utf8')
process.stdin.on('data', (chunk) => {
  inputBuffer += chunk
  let newlineIndex = inputBuffer.indexOf('\n')
  while (newlineIndex !== -1) {
    const line = inputBuffer.slice(0, newlineIndex)
    inputBuffer = inputBuffer.slice(newlineIndex + 1)
    handleLine(line)
    newlineIndex = inputBuffer.indexOf('\n')
  }
})
