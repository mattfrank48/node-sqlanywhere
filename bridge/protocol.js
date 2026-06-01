'use strict'

function encode(value) {
  if (Buffer.isBuffer(value)) {
    return { __sqlanywhereType: 'Buffer', data: value.toString('base64') }
  }
  if (typeof value === 'bigint') {
    return { __sqlanywhereType: 'BigInt', data: value.toString() }
  }
  if (Array.isArray(value)) {
    return value.map(encode)
  }
  if (value && typeof value === 'object') {
    const out = {}
    for (const [key, val] of Object.entries(value)) {
      out[key] = encode(val)
    }
    return out
  }
  return value
}

function decode(value) {
  if (Array.isArray(value)) {
    return value.map(decode)
  }
  if (value && typeof value === 'object') {
    if (value.__sqlanywhereType === 'Buffer' && typeof value.data === 'string') {
      return Buffer.from(value.data, 'base64')
    }
    if (value.__sqlanywhereType === 'BigInt' && typeof value.data === 'string') {
      return BigInt(value.data)
    }

    const out = {}
    for (const [key, val] of Object.entries(value)) {
      out[key] = decode(val)
    }
    return out
  }
  return value
}

function serializeLine(payload) {
  return `${JSON.stringify(encode(payload))}\n`
}

function parseLine(line) {
  return decode(JSON.parse(line))
}

module.exports = {
  serializeLine,
  parseLine,
}
