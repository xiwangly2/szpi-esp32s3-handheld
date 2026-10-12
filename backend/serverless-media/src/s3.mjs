import { S3Client, GetObjectCommand, PutObjectCommand, HeadObjectCommand } from '@aws-sdk/client-s3';
import { getSignedUrl } from '@aws-sdk/s3-request-presigner';

export function s3Storage(env) {
  const client = new S3Client({
    region: env.STORAGE_REGION || env.AWS_REGION,
    ...(env.STORAGE_ENDPOINT ? { endpoint: env.STORAGE_ENDPOINT } : {}),
    ...(env.STORAGE_ACCESS_KEY ? { credentials: { accessKeyId: env.STORAGE_ACCESS_KEY, secretAccessKey: env.STORAGE_SECRET_KEY,
      ...(env.STORAGE_SESSION_TOKEN ? { sessionToken: env.STORAGE_SESSION_TOKEN } : {}) } } : {}),
    requestChecksumCalculation: 'WHEN_REQUIRED',
  });
  const Bucket = env.VIDEO_BUCKET;
  return {
    async head(Key) {
      try { const result = await client.send(new HeadObjectCommand({ Bucket, Key })); return { size: result.ContentLength }; }
      catch (error) { if (error.$metadata?.httpStatusCode === 404) return null; throw error; }
    },
    download: (Key, expiresIn) => getSignedUrl(client, new GetObjectCommand({ Bucket, Key }), { expiresIn }),
    async upload(Key, info, expiresIn) {
      const headers = { 'Content-Type': info.contentType, 'Content-Length': String(info.size),
        'Content-MD5': info.md5, 'If-None-Match': '*', 'x-amz-meta-sha256': info.sha256 };
      const url = await getSignedUrl(client, new PutObjectCommand({ Bucket, Key, ContentType: info.contentType,
        ContentLength: info.size, ContentMD5: info.md5, IfNoneMatch: '*', Metadata: { sha256: info.sha256 } }), {
        expiresIn, unhoistableHeaders: new Set(['x-amz-meta-sha256']), signableHeaders: new Set(['content-type']),
      });
      return { url, headers };
    },
  };
}
