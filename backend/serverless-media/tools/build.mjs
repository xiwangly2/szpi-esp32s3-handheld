import { build } from 'esbuild';
import { mkdir } from 'node:fs/promises';
await mkdir('dist', { recursive: true });
await build({ entryPoints: ['src/node.mjs'], outfile: 'dist/index.js', bundle: true, platform: 'node', target: 'node22', format: 'cjs',
  external: ['proxy-agent'] }); // Optional urllib feature; proxies are not enabled by the cloud adapters.
await build({ entryPoints: ['src/worker.mjs'], outfile: 'dist/worker.mjs', bundle: true, platform: 'browser', target: 'es2022', format: 'esm' });
await build({ entryPoints: ['src/container.mjs'], outfile: 'dist/server.cjs', bundle: true, platform: 'node', target: 'node22', format: 'cjs', external: ['proxy-agent'] });
console.log('Built Node function, HTTP container, and Cloudflare Worker bundles.');
