import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// edge-box Web 配置界面
// 开发模式：npm run dev 后浏览器访问 http://localhost:5173
// 生产模式：npm run build 产物 dist/ 由 C++ WebServer 直接从 web/dist 托管
export default defineConfig({
  plugins: [react()],
  base: './', // 相对路径，兼容边缘盒简单静态托管
  server: {
    port: 5173,
    proxy: {
      // 开发代理指向边缘盒 C++ Web API（默认 8180）
      '/api': {
        target: 'http://127.0.0.1:8180',
        changeOrigin: true,
      },
    },
  },
  build: {
    outDir: 'dist',
    chunkSizeWarningLimit: 1200,
  },
})