import { ApiResp, client, del, get, post, put } from './client'
import type {
  Customer,
  Device,
  FlowRow,
  ImportResult,
  MatchRecord,
  PageResult,
  RecognitionRecord,
  StaffRow,
  Store,
  SystemConfig,
  VerifyRecord,
  VisitStats,
} from './types'

// ---- 设备 ----
// 后端返回 {items:[...]}，此处解包为数组（Dashboard/Devices 均按数组使用）
export const fetchDeviceTree = async () => {
  const data = await get<{ items: Device[] }>('/devices')
  return data.items
}
export const fetchDeviceConfig = (id: number) =>
  get<{ device_id: number; config: Record<string, unknown> | null }>(`/devices/${id}/config`)
export const pushDeviceConfig = (id: number, config: Record<string, unknown>) =>
  put<{ device_id: number }>(`/devices/${id}/config`, { config })

export const updateDevice = (id: number, body: Record<string, unknown>) =>
  put<{ device_id: number }>(`/devices/${id}`, body)

// ---- 人员库 ----
export const fetchCustomers = (params: {
  page?: number
  page_size?: number
  person_type?: number
  store_id?: number
  name?: string
  status?: number
}) => get<PageResult<Customer>>('/customers', params)

export const createCustomer = (body: Record<string, unknown>) =>
  post<{ customer_id: number; version: number; history?: VisitStats }>('/customers', body)

export const updateCustomer = (id: number, body: Record<string, unknown>) =>
  put<{ customer_id: number; version: number }>(`/customers/${id}`, body)

export const deleteCustomer = (id: number, extra?: { staff?: boolean }) =>
  del<{ customer_id: number }>(`/customers/${id}`, extra)

export const appendCustomerFeature = (id: number, body: { face_feature?: string; id_photo?: string }) =>
  post<{ feature_id: number }>(`/customers/${id}/features`, body)

// ---- 历史来访回查 ----
export const historySearch = (body: {
  face_feature: string
  similarity_threshold?: number
  top_k?: number
  scope?: { store_ids?: number[]; start_at?: string; end_at?: string }
}) =>
  post<{
    total_visits: number
    visit_days: number
    first_visit_at: string
    last_visit_at: string
    matched_records: MatchRecord[]
  }>('/history/search', body)

// ---- 统计 ----
export const fetchFlowStats = (params: Record<string, unknown>) =>
  get<{ items: FlowRow[]; total: { in: number; out: number; unique_persons?: number } }>('/stats/flow', params)

export const fetchStaffStats = (params: { start_at: string; end_at?: string }) =>
  get<{ items: StaffRow[] }>('/stats/staff', params)

export const fetchVisitStats = (customerId: number) => get<VisitStats>(`/stats/visits/${customerId}`)
// ---- 门店 ----
export const fetchStores = (params?: Record<string, unknown>) =>
  get<{ items: Store[] }>('/stores', params)
export const createStore = (body: { name: string; address?: string }) =>
  post<{ store_id: number }>('/stores', body)
export const updateStore = (id: number, body: { name: string; address?: string; status?: number }) =>
  put<{ store_id: number }>(`/stores/${id}`, body)
export const deleteStore = (id: number) => del<{ store_id: number }>(`/stores/${id}`)

// ---- 记录查询 ----
export const fetchRecognitionRecords = (params: Record<string, unknown>) =>
  get<PageResult<RecognitionRecord>>('/records/recognition', params)
export const fetchVerifyRecords = (params: Record<string, unknown>) =>
  get<PageResult<VerifyRecord>>('/records/verify', params)
// ---- 全局系统配置 ----
export const fetchSystemConfig = () => get<SystemConfig>('/system/config')
export const saveSystemConfig = (body: { face_service_url?: string; face_service_key?: string }) =>
  put<{ updated: boolean }>('/system/config', body)

// ---- 人员导出 ----
export const exportCustomersXLSX = (params: Record<string, unknown>) =>
  client.get<Blob>('/export/customers.xlsx', { params, responseType: 'blob' }).then(({ data }) => {
    const blob = data instanceof Blob ? data : new Blob([data as unknown as BlobPart])
    const link = document.createElement('a')
    link.href = URL.createObjectURL(blob)
    link.download = 'customers.xlsx'
    link.click()
    URL.revokeObjectURL(link.href)
  })

// ---- 人员批量导入（Excel 模板）----
export const downloadImportTemplate = () =>
  client.get<Blob>('/customers/import/template', { responseType: 'blob' }).then(({ data }) => {
    const blob = data instanceof Blob ? data : new Blob([data as unknown as BlobPart])
    const link = document.createElement('a')
    link.href = URL.createObjectURL(blob)
    link.download = 'faceflow_customers_import_template.xlsx'
    link.click()
    URL.revokeObjectURL(link.href)
  })

export const importCustomers = (file: File) => {
  const fd = new FormData()
  fd.append('file', file)
  return client
    .post<ApiResp<ImportResult>>('/customers/import', fd)
    .then((r) => {
      if (r.data.code !== 0) throw new Error(r.data.message || `错误码 ${r.data.code}`)
      return r.data.data
    })
}