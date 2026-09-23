import axios from 'axios'

// 与后端 13.0 统一响应体一致
export interface ApiResp<T = unknown> {
  code: number
  message: string
  data: T
}

export const client = axios.create({
  baseURL: '/api/v1',
  timeout: 15000,
})

// ---- Token 管理 ----
export function getToken(): string { return localStorage.getItem('token') ?? '' }
export function setToken(t: string) { localStorage.setItem('token', t) }
export function clearToken() { localStorage.removeItem('token') }

client.interceptors.request.use((cfg) => {
  const t = getToken()
  if (t) cfg.headers.Authorization = `Bearer ${t}`
  return cfg
})

// 统一错误处理：非 0 code 抛错；401 清除 token 并跳登录
client.interceptors.response.use(
  (resp) => {
    const body = resp.data as ApiResp
    if (body && typeof body.code === 'number' && body.code !== 0) {
      return Promise.reject(new Error(body.message || `错误码 ${body.code}`))
    }
    return resp
  },
  (err) => {
    if (err?.response?.status === 401) {
      clearToken()
      if (!window.location.pathname.startsWith('/login')) {
        window.location.href = '/login'
      }
    }
    return Promise.reject(err)
  },
)

export async function get<T>(url: string, params?: Record<string, unknown>): Promise<T> {
  const { data } = await client.get<ApiResp<T>>(url, { params })
  return data.data
}

export async function post<T>(url: string, body?: unknown): Promise<T> {
  const { data } = await client.post<ApiResp<T>>(url, body)
  return data.data
}

export async function put<T>(url: string, body?: unknown): Promise<T> {
  const { data } = await client.put<ApiResp<T>>(url, body)
  return data.data
}

export async function del<T>(url: string, params?: Record<string, unknown>): Promise<T> {
  const { data } = await client.delete<ApiResp<T>>(url, { params })
  return data.data
}
// 统一提取错误消息（后端 Error / 网络错误）
export function errMsg(e: unknown): string {
  return e instanceof Error ? e.message : String(e)
}
