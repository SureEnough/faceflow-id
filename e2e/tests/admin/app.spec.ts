import { test, expect, type Page } from '@playwright/test'

// 与 e2e/helpers/start-admin.mjs 后端环境一致
const USER = 'admin'
const PASS = 'admin123'
const PSK = 'e2e-psk'

async function login(page: Page, username: string, password: string) {
  await page.goto('/login')
  await page.locator('#username').fill(username)
  await page.locator('#password').fill(password)
  await page.getByRole('button', { name: /登\s*录/ }).click()
}

test.describe('admin-backend 管理后台', () => {
  // Fixture：注册一台主设备（设备树/配置下发用例需要）
  let deviceId = 0
  test.beforeAll(async ({ request }) => {
    const resp = await request.post('/api/v1/devices/register', {
      data: { device_type: 1, name: 'E2E边缘盒', store_id: 1, psk: PSK },
    })
    expect(resp.status()).toBe(200)
    const body = await resp.json()
    deviceId = body.data.device_id
    expect(deviceId).toBeGreaterThan(0)
  })

  test('登录页可见且可渲染', async ({ page }) => {
    await page.goto('/login')
    await expect(page.getByText('FaceFlow')).toBeVisible()
    await expect(page.locator('#username')).toBeVisible()
    await expect(page.locator('#password')).toBeVisible()
  })

  test('错误密码停留在登录页', async ({ page }) => {
    await login(page, USER, 'wrong-pass')
    await page.waitForTimeout(800)
    expect(page.url()).toContain('/login')
  })

  test('正确登录进入仪表盘', async ({ page }) => {
    await login(page, USER, PASS)
    await expect(page).toHaveURL(/\/$/, { timeout: 10_000 })
    await expect(page.getByText('后端服务：在线')).toBeVisible()
    await expect(page.getByText('主设备（边缘盒/录入端）')).toBeVisible()
  })

  test('设备树渲染配置下发弹窗', async ({ page }) => {
    await login(page, USER, PASS)
    await expect(page).toHaveURL(/\/$/)
    await page.goto('/devices')
    await expect(page.getByText('E2E边缘盒')).toBeVisible()

    await page.getByRole('button', { name: '配置下发' }).click()
    await expect(page.getByText('向主设备下发配置')).toBeVisible()
    // 选择设备后自动拉取配置到文本框
    await page.locator('#device_id').click()
    await page.locator('.ant-select-item-option', { hasText: 'E2E边缘盒' }).click()
    await expect(page.locator('#config')).toHaveValue(/\{/, { timeout: 8_000 })
    await page.getByRole('button', { name: /取\s*消/ }).click()
  })

  test('客流统计空态提示', async ({ page }) => {
    await login(page, USER, PASS)
    await expect(page).toHaveURL(/\/$/)
    await page.goto('/stats')
    await expect(page.getByText(/暂无客流数据/)).toBeVisible()
  })

  test('API 契约：health / 登录 / 配置下发', async ({ request }) => {
    // 后端健康检查（直连 18089）
    const health = await request.get('http://127.0.0.1:18089/api/v1/health')
    expect(health.status()).toBe(200)
    const hb = await health.json()
    expect(hb.data.status).toBe('up')

    // 错误密码 → 401
    const bad = await request.post('/api/v1/auth/login', {
      data: { username: USER, password: 'x' },
    })
    expect(bad.status()).toBe(401)

    // 正确登录 → token
    const ok = await request.post('/api/v1/auth/login', {
      data: { username: USER, password: PASS },
    })
    expect(ok.status()).toBe(200)
    const loginBody = await ok.json()
    const token = loginBody.data.token as string
    expect(token.length).toBeGreaterThan(20)

    // 配置下发 → 拉取回读
    const cfgBody = {
      det_thresh: 0.77,
      cameras: [{ camera_id: 'cam-e2e', url: 'rtsp://10.0.0.9/stream' }],
    }
    const put = await request.put(`/api/v1/devices/${deviceId}/config`, {
      data: { config: cfgBody },
      headers: { Authorization: `Bearer ${token}` },
    })
    expect(put.status()).toBe(200)

    const get = await request.get(`/api/v1/devices/${deviceId}/config`, {
      headers: { Authorization: `Bearer ${token}` },
    })
    expect(get.status()).toBe(200)
    const getBody = await get.json()
    expect(getBody.data.config.det_thresh).toBe(0.77)
  })
})