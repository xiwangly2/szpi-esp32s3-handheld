import { AwsClient } from 'aws4fetch';
import { createMediaService } from './service.mjs';
import { createInformationProvider } from './information.mjs';

const providers = new WeakMap();

export default {
  async fetch(request, env) {
    if (!providers.has(env)) providers.set(env, createInformationProvider(env.INFO_CONFIG_JSON));
    const signer = new AwsClient({ accessKeyId: env.R2_ACCESS_KEY_ID, secretAccessKey: env.R2_SECRET_ACCESS_KEY,
      service: 's3', region: 'auto' });
    const signed = async (key, method, expires, headers = {}) => {
      const url = new URL(`https://${env.R2_ACCOUNT_ID}.r2.cloudflarestorage.com/${env.VIDEO_BUCKET}/${key}`);
      url.searchParams.set('X-Amz-Expires', String(expires));
      return (await signer.sign(url, { method, headers, aws: { signQuery: true, allHeaders: true } })).url;
    };
    const store = {
      async head(key) { const object = await env.MEDIA.head(key); return object ? { size: object.size } : null; },
      download: (key, seconds) => signed(key, 'GET', seconds),
      async upload(key, info, seconds) {
        const headers = { 'Content-Type': info.contentType, 'Content-Length': String(info.size), 'Content-MD5': info.md5,
          'If-None-Match': '*', 'x-amz-meta-sha256': info.sha256 };
        return { url: await signed(key, 'PUT', seconds, headers), headers };
      },
    };
    return createMediaService({ store, readToken: env.READ_TOKEN, adminToken: env.ADMIN_TOKEN,
      information: providers.get(env) })(request);
  },
};
