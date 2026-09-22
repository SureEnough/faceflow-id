// 启动边缘盒供 E2E 使用（Playwright webServer 入口，edge-box 项目）。
// 用法: node helpers/start-edge-box.mjs
// 说明: 需要先构建 C++（edge-box/build/edge_box）与前端（edge-box/web/dist）。
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import fs from 'node:fs'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(__dirname, '../..')
const bin = process.env.EDGE_BOX_BIN || path.resolve(repoRoot, 'edge-box/build/edge_box')
const cfg = path.resolve(__dirname, '../tests/edge-box/edge_box.e2e.json')
const staticDir = path.resolve(repoRoot, 'edge-box/web/dist')

if (!fs.existsSync(bin)) {
  console.error(`edge_box binary not found: ${bin}\n请先构建: cd edge-box && cmake -B build && cmake --build build`)
  process.exit(1)
}
if (!fs.existsSync(staticDir)) {
  console.warn(`前端产物缺失: ${staticDir}，请先构建: cd edge-box/web && npm run build`)
}

const child = spawn(bin, ['-c', cfg, '-backend', process.env.EDGE_BOX_BACKEND || 'mock'], {
  cwd: path.dirname(cfg), // 数据文件写到 tests/edge-box/ 下（已 gitignore）
  stdio: process.env.DEBUG ? 'inherit' : ['ignore', 'pipe', 'pipe'],
})
child.stdout.on('data', (d) => process.env.DEBUG && process.stdout.write(d))
child.stderr.on('data', (d) => process.stderr.write(d))
child.on('exit', (code, sig) => {
  console.log(`[e2e] edge_box exited (code=${code}, sig=${sig})`)
  process.exit(code ?? 0)
})
for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, () => child.kill(sig))