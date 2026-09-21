import { get, post } from './client'
import type {
  Customer,
  Device,
  FlowRow,
  MatchRecord,
  PageResult,
  StaffRow,
  VisitStats,
} from './types'

// ---- 设备 ----
export const fetchDeviceTree = () => get<Device[]>('/devices')

// ---- 人员库 ----
export const fetchCustomers = (params: {
  page?: number
  page_size?: number
  person_type?: number
  name?: string
  status?: number
}) => get<PageResult<Customer>>('/customers', params)

export const createCustomer = (body: Record<string, unknown>) =>
  post<{ customer_id: number; version: number; history?: VisitStats }>('/customers', body)

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
export const fetchFlowStats = (params: {
  granularity?: string
  person_type?: number
  start_at: string
  end_at?: string
}) => get<{ items: FlowRow[]; total: { in: number; out: number } }>('/stats/flow', params)

export const fetchStaffStats = (params: { start_at: string; end_at?: string }) =>
  get<{ items: StaffRow[] }>('/stats/staff', params)

export const fetchVisitStats = (customerId: number) => get<VisitStats>(`/stats/visits/${customerId}`)