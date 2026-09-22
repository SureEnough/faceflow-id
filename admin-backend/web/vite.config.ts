import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// 代理目标可通过环境变量覆盖（E2E 用 E2E_API_TARGET 指向测试后端）
const apiTarget = process.env.E2E_API_TARGET || 'http://127.0.0.1:8080'

export default defineConfig({
  plugins: [react()],
  // 开发模式：npm run dev
  server: {
    port: 5173,
    proxy: {
      '/api': { target: apiTarget, changeOrigin: true },
    },
  },
  // 生产预览（E2E 使用）：npm run preview 起 dist + 反代 /api
  preview: {
    port: 18192,
    proxy: {
      '/api': { target: apiTarget, changeOrigin: true },
    },
  },
})