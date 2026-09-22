import axios from 'axios'
import type { EdgeBoxConfig, EdgeBoxStatus, Snapshot } from './types'

export interface ApiResp<T> {
  code: number
  message: string
  data: T
}

const AUTH_KEY = 'edge_box_auth' // base64("user:pass")

export function getAuthB64(): string {
  return localStorage.getItem(AUTH_KEY) ?? ''
}
export function setAuthB64(b64: string): void {
  localStorage.setItem(AUTH_KEY, b64)
}
export function clearAuthB64(): void {
  localStorage.removeItem(AUTH_KEY)
}

export const api = axios.create({ baseURL: '/api', timeout: 10000 })

api.interceptors.request.use((cfg) => {
  const a = getAuthB64()
  if (a) cfg.headers.Authorization = `Basic ${a}`
  return cfg
})

api.interceptors.response.use(
  (resp) => resp,
  (err) => {
    if (err?.response?.status === 401) {
      window.dispatchEvent(new CustomEvent('edgebox:auth'))
    }
    return Promise.reject(err)
  },
)

async function unwrap<T>(p: Promise<{ data: ApiResp<T> }>): Promise<T> {
  const { data } = await p
  if (data && typeof data.code === 'number' && data.code !== 0) {
    throw new Error(data.message || 'error')
  }
  return data.data
}

export const fetchStatus = () => unwrap<EdgeBoxStatus>(api.get('/status'))
export const fetchConfig = () => unwrap<EdgeBoxConfig>(api.get('/config'))
export const saveConfig = (cfg: Partial<EdgeBoxConfig>) =>
  unwrap<Record<string, never>>(api.put('/config', cfg))
export const triggerReload = () => unwrap<Record<string, never>>(api.post('/reload'))
export const fetchSnapshots = (limit = 20) =>
  unwrap<Snapshot[]>(api.get('/snapshots', { params: { limit } }))
export const triggerRestart = () => unwrap<Record<string, never>>(api.post('/restart'))