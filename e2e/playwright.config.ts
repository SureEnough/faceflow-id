import { defineConfig } from '@playwright/test'

/**
 * FaceFlow 统一 E2E（Playwright + Chromium）
 *
 * 两个项目（frontend → 后端依赖）：
 *   - edge-box ：边缘盒 Web 控制台（C++ edge_box 服务，端口 18191）
 *   - admin    ：管理后台（Go bin/server + vite preview 反代，端口 18192 / 18089）
 *
 * 注意：@playwright/test@1.40 只支持顶层 webServer（数组），
 * 因此统一在顶层启动全部服务；只跑单项目时也会拉起多余服务，属预期。
 *
 * 运行：
 *   1) 构建后端（一次性）
 *      cd edge-box && cmake -B build && cmake --build build
 *      cd admin-backend && go build -o bin/server ./cmd/server
 *      cd admin-backend/web && npm install && npm run build
 *   2) 安装浏览器（一次性；国内镜像加速）
 *      cd e2e && PLAYWRIGHT_DOWNLOAD_HOST=https://npmmirror.com/mirrors/playwright npm run e2e:install
 *   3) 跑测试
 *      cd e2e && npm install && npm run e2e
 */
export default defineConfig({
  testDir: './tests',
  fullyParallel: false,
  timeout: 30_000,
  expect: { timeout: 8_000 },
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  workers: 1,

  use: {
    viewport: { width: 1280, height: 900 },
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },

  // 顶层统一拉起全部后端依赖
  webServer: [
    {
      command: 'node helpers/start-edge-box.mjs',
      url: 'http://127.0.0.1:18191/', // 首页（未登录也返回 200）
      reuseExistingServer: !process.env.CI,
      timeout: 30_000,
    },
    {
      command: 'node helpers/start-admin.mjs backend',
      url: 'http://127.0.0.1:18089/api/v1/health',
      reuseExistingServer: !process.env.CI,
      timeout: 30_000,
    },
    {
      command: 'node helpers/start-admin.mjs preview',
      url: 'http://127.0.0.1:18192/',
      reuseExistingServer: !process.env.CI,
      timeout: 30_000,
    },
  ],

  projects: [
    {
      name: 'edge-box',
      testDir: './tests/edge-box',
      use: { baseURL: 'http://127.0.0.1:18191' },
    },
    {
      name: 'admin',
      testDir: './tests/admin',
      use: { baseURL: 'http://127.0.0.1:18192' },
    },
  ],

  reporter: [
    ['list'],
    ['html', { outputFolder: 'playwright-report', open: 'never' }],
  ],
  outputDir: 'test-results',
})