import COS from 'cos-nodejs-sdk-v5';
import OSS from 'ali-oss';

export function cosStorage(env) {
  const client = new COS({ SecretId: env.STORAGE_ACCESS_KEY, SecretKey: env.STORAGE_SECRET_KEY,
    ...(env.STORAGE_SESSION_TOKEN ? { SecurityToken: env.STORAGE_SESSION_TOKEN } : {}) });
  const base = { Bucket: env.VIDEO_BUCKET, Region: env.STORAGE_REGION };
  const signed = args => new Promise((resolve, reject) => client.getObjectUrl({ ...base, ...args, Sign: true },
    (error, data) => error ? reject(error) : resolve(data.Url)));
  return {
    async head(Key) {
      try { const result = await new Promise((resolve, reject) => client.headObject({ ...base, Key }, (e, r) => e ? reject(e) : resolve(r)));
        return { size: Number(result.headers['content-length']) }; }
      catch (error) { if (error.statusCode === 404) return null; throw error; }
    },
    download: (Key, Expires) => signed({ Key, Method: 'GET', Expires }),
    async upload(Key, info, Expires) {
      const headers = { 'Content-Type': info.contentType, 'Content-Length': String(info.size), 'Content-MD5': info.md5,
        'x-cos-forbid-overwrite': 'true', 'x-cos-meta-sha256': info.sha256 };
      return { url: await signed({ Key, Method: 'PUT', Expires, Headers: headers }), headers };
    },
  };
}

export function ossStorage(env) {
  if (env.URLLIB_ENABLE_PROXY) throw new Error('This function bundle does not support urllib proxy mode');
  const client = new OSS({ region: env.STORAGE_REGION, bucket: env.VIDEO_BUCKET,
    accessKeyId: env.STORAGE_ACCESS_KEY, accessKeySecret: env.STORAGE_SECRET_KEY, secure: true,
    ...(env.STORAGE_SESSION_TOKEN ? { stsToken: env.STORAGE_SESSION_TOKEN } : {}) });
  return {
    async head(key) { try { const result = await client.head(key); return { size: Number(result.res.headers['content-length']) }; }
      catch (error) { if (error.status === 404) return null; throw error; } },
    download: (key, expires) => client.signatureUrl(key, { expires, method: 'GET' }),
    async upload(key, info, expires) {
      const headers = { 'Content-Type': info.contentType, 'Content-Length': String(info.size), 'Content-MD5': info.md5,
        'x-oss-forbid-overwrite': 'true', 'x-oss-meta-sha256': info.sha256 };
      return { url: client.signatureUrl(key, { expires, method: 'PUT', ...headers }), headers };
    },
  };
}
