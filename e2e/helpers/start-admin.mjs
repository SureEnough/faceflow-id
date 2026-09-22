// 启动 admin 后端（Go bin/server）或前端静态预览（vite preview）供 E2E 使用。
// 用法: node helpers/start-admin.mjs backend | preview
// 说明: 需要先构建后端（admin-backend/bin/server）与前端（admin-backend/web/dist）。
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import fs from 'node:fs'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(__dirname, '../..')
const runtimeDir = path.resolve(repoRoot, '.runtime/admin-e2e')
fs.mkdirSync(runtimeDir, { recursive: true })

const BACKEND_PORT = 18089
const PREVIEW_PORT = 18192

function ensureBackendBin() {
  const bin = path.resolve(repoRoot, 'admin-backend/bin/server')
  if (fs.existsSync(bin)) return bin
  console.error('admin-backend/bin/server 不存在，请先构建: cd admin-backend && go build -o bin/server ./cmd/server')
  process.exit(1)
}

const mode = process.argv[2]
if (mode === 'backend') {
  const bin = ensureBackendBin()
  const child = spawn(bin, [], {
    cwd: runtimeDir,
    env: {
      ...process.env,
      HTTP_ADDR: `127.0.0.1:${BACKEND_PORT}`,
      DB_DSN: 'sqlite:./admin-e2e.db',
      DEVICE_PSK: 'e2e-psk',
      JWT_SECRET: 'e2e-secret',
      ADMIN_USER: 'admin',
      ADMIN_PASSWORD: 'admin123',
      RETENTION_DAYS: '365',
    },
    stdio: process.env.DEBUG ? 'inherit' : ['ignore', 'pipe', 'pipe'],
  })
  child.stdout.on('data', (d) => process.env.DEBUG && process.stdout.write(d))
  child.stderr.on('data', (d) => process.stderr.write(d))
  child.on('exit', (code, sig) => {
    console.log(`[e2e] admin backend exited (code=${code}, sig=${sig})`)
    process.exit(code ?? 0)
  })
  for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, () => child.kill(sig))
} else if (mode === 'preview') {
  const webDir = path.resolve(repoRoot, 'admin-backend/web')
  if (!fs.existsSync(path.join(webDir, 'dist/index.html'))) {
    console.error('admin-backend/web/dist 不存在，请先构建: cd admin-backend/web && npm install && npm run build')
    process.exit(1)
  }
  // 优先用本地依赖；未安装时提示
  const viteBin = path.join(webDir, 'node_modules/.bin/vite')
  if (!fs.existsSync(viteBin)) {
    console.error('admin-backend/web/node_modules 不存在，请先: cd admin-backend/web && npm install')
    process.exit(1)
  }
  const child = spawn(viteBin, ['preview', '--host', '127.0.0.1', '--port', String(PREVIEW_PORT), '--strictPort'], {
    cwd: webDir,
    env: { ...process.env, E2E_API_TARGET: `http://127.0.0.1:${BACKEND_PORT}` },
    stdio: process.env.DEBUG ? 'inherit' : ['ignore', 'pipe', 'pipe'],
  })
  child.stdout.on('data', (d) => process.env.DEBUG && process.stdout.write(d))
  child.stderr.on('data', (d) => process.stderr.write(d))
  child.on('exit', (code, sig) => {
    console.log(`[e2e] admin preview exited (code=${code}, sig=${sig})`)
    process.exit(code ?? 0)
  })
  for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, () => child.kill(sig))
} else {
  console.error('用法: node helpers/start-admin.mjs backend | preview')
  process.exit(1)
}
