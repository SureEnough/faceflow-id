import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// 开发代理：/api → Go 后端（默认 :8080）
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:8080',
        changeOrigin: true,
      },
    },
  },
})