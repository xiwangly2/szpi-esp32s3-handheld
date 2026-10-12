export function httpAdapter(service) {
  return async (incoming, outgoing) => {
    try {
      const chunks = [];
      let size = 0;
      for await (const chunk of incoming) {
        size += chunk.length;
        if (size > 2048) { outgoing.writeHead(413); outgoing.end(); return; }
        chunks.push(chunk);
      }
      const method = incoming.method || 'GET';
      const body = Buffer.concat(chunks);
      const response = await service(new Request(`https://media.invalid${incoming.url || '/'}`, {
        method, headers: incoming.headers,
        ...(!['GET', 'HEAD'].includes(method) && body.length ? { body } : {}),
      }));
      outgoing.writeHead(response.status, Object.fromEntries(response.headers));
      outgoing.end(Buffer.from(await response.arrayBuffer()));
    } catch (error) {
      console.error('HTTP adapter failed', error?.name || 'Error');
      if (!outgoing.headersSent) outgoing.writeHead(500);
      outgoing.end();
    }
  };
}
