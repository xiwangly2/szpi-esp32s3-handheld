import { createServer } from 'node:http';
import { nodeService } from './node.mjs';
import { httpAdapter } from './http.mjs';
createServer(httpAdapter(nodeService())).listen(Number(process.env.PORT || 9000), process.env.HOST || '0.0.0.0');
