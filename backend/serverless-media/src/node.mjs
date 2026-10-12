import { webcrypto } from 'node:crypto';
import { createMediaService } from './service.mjs';
import { s3Storage } from './s3.mjs';
import { cosStorage, ossStorage } from './other-storage.mjs';
globalThis.crypto ??= webcrypto;
let service;

export function nodeService(env = process.env) {
  const type = env.STORAGE_PROVIDER || 's3';
  const factory = { s3: s3Storage, cos: cosStorage, oss: ossStorage }[type];
  if (!factory) throw new Error('Unsupported storage provider');
  return createMediaService({ store: factory(env), readToken: env.READ_TOKEN, adminToken: env.ADMIN_TOKEN });
}

// API Gateway HTTP API v2, Lambda URLs, and SCF API Gateway event envelopes.
export async function handler(event) {
  service ??= nodeService();
  const method = event.requestContext?.http?.method || event.httpMethod || 'GET';
  const path = event.rawPath || event.path || '/';
  const query = event.rawQueryString ?? new URLSearchParams(event.queryStringParameters || event.queryString || {}).toString();
  const headers = event.headers || {};
  const body = event.isBase64Encoded ? Buffer.from(event.body || '', 'base64') : event.body;
  const request = new Request(`https://media.invalid${path}${query ? `?${query}` : ''}`, {
    method, headers, ...(!['GET', 'HEAD'].includes(method) && body ? { body } : {}),
  });
  const response = await service(request);
  return { statusCode: response.status, headers: Object.fromEntries(response.headers), body: await response.text(), isBase64Encoded: false };
}

export const main_handler = handler;
