FROM node:22-bookworm-slim
WORKDIR /service
COPY package.json ./
RUN npm install --omit=dev
COPY src ./src
EXPOSE 8080
CMD ["node", "--import", "tsx", "src/server.ts"]
