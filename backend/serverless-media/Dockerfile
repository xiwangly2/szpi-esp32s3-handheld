FROM node:22-bookworm-slim
WORKDIR /app
COPY dist/server.cjs ./server.cjs
USER node
ENV PORT=9000
EXPOSE 9000
CMD ["node", "server.cjs"]
