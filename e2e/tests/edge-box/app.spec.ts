import { test, expect, type Page } from '@playwright/test'

// 与 tests/edge-box/edge_box.e2e.json 保持一致
const USER = 'admin'
const PASS = 'e2e-pass'

// 前端登录态独立于 HTTP Basic：出现登录弹窗时通过表单登录
async function loginIfNeeded(page: Page) {
  if (await page.getByText('登录边缘盒').isVisible().catch(() => false)) {
    await page.locator('#username').fill(USER)
    await page.locator('#password').fill(PASS)
    await page.locator('button[type="submit"]').click()
    await expect(page.getByRole('tab', { name: '运行状态' })).toBeVisible()
  }
}

test.describe('边缘盒 Web 配置界面', () => {
  test('未登录显示登录弹窗', async ({ page }) => {
    await page.goto('/')
    await expect(page.getByText('登录边缘盒')).toBeVisible()
    await expect(page.getByText(/请登录/)).toBeVisible()
  })

  test('凭据错误时 API 返回 401', async ({ page }) => {
    const resp = await page.request.get('/api/config', {
      headers: { Authorization: 'Basic ' + Buffer.from('admin:wrong').toString('base64') },
    })
    expect(resp.status()).toBe(401)
  })

  test('登录后状态页展示运行状态', async ({ page }) => {
    await page.goto('/')
    await loginIfNeeded(page)
    await expect(page.getByRole('tab', { name: '运行状态' })).toBeVisible()
    await expect(page.getByText('设备 ID')).toBeVisible()
    await expect(page.getByText('cam-ui')).toBeVisible()
    await expect(page.getByText('客流进')).toBeVisible()
    await expect(page.getByText('版本', { exact: true })).toBeVisible()
  })

  test('摄像头页可编辑（含虚拟线）', async ({ page }) => {
    await page.goto('/')
    await loginIfNeeded(page)
    await page.getByRole('tab', { name: '摄像头' }).click()
    await expect(page.getByText('摄像头配置')).toBeVisible()
    await expect(page.getByText(/URL（rtsp:\/\/ 或 usb0）/)).toBeVisible()
    await expect(page.locator('.ant-card-head-title', { hasText: 'cam-ui' })).toBeVisible()
    await expect(page.getByText('虚拟线 起点 x,y')).toBeVisible()
    await expect(page.getByRole('button', { name: /保存/ }).first()).toBeVisible()
  })

  test('全局参数页字段齐全', async ({ page }) => {
    await page.goto('/')
    await loginIfNeeded(page)
    await page.getByRole('tab', { name: '全局参数' }).click()
    await expect(page.getByText('检测阈值')).toBeVisible()
    await expect(page.getByText('上报端点')).toBeVisible()
    await expect(page.getByText('记录保留天数')).toBeVisible()
  })

  test('API Basic Auth 契约', async ({ request }) => {
    const unauth = await request.get('/api/config')
    expect(unauth.status()).toBe(401)

    const ok = await request.get('/api/config', {
      headers: { Authorization: 'Basic ' + Buffer.from(`${USER}:${PASS}`).toString('base64') },
    })
    expect(ok.status()).toBe(200)
    const body = await ok.json()
    expect(Array.isArray(body.data.cameras)).toBe(true)
  })

  test('状态接口返回相机客流', async ({ request }) => {
    const resp = await request.get('/api/status', {
      headers: { Authorization: 'Basic ' + Buffer.from(`${USER}:${PASS}`).toString('base64') },
    })
    expect(resp.status()).toBe(200)
    const body = await resp.json()
    expect(body.data.cameras.length).toBeGreaterThan(0)
    expect(body.data.cameras[0]).toHaveProperty('camera_id')
    expect(body.data.cameras[0]).toHaveProperty('flow_in')
  })
})