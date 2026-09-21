import { useEffect, useState } from 'react'
import { Card, Input, Table, Tag, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import { client, getToken, errMsg } from '../api/client'
import type { ApiResp } from '../api/client'

interface AuditRow {
  id: number
  username: string
  action: string
  target_type: string
  target_id: number
  detail: string
  ip: string
  created_at: string
}

const columns: ColumnsType<AuditRow> = [
  { title: '时间', dataIndex: 'created_at', width: 180 },
  { title: '操作人', dataIndex: 'username', width: 120 },
  {
    title: '动作', dataIndex: 'action', render: (v: string) => {
      const color = v.startsWith('DELETE') ? 'red' : v.startsWith('PUT') ? 'orange' : v.startsWith('POST') ? 'blue' : 'default'
      return <Tag color={color}>{v}</Tag>
    },
  },
  { title: '目标', dataIndex: 'target_type', width: 100, render: (v: string, r) => (v ? `${v}#${r.target_id}` : '-') },
  { title: '详情', dataIndex: 'detail', ellipsis: true },
  { title: 'IP', dataIndex: 'ip', width: 120 },
]

export default function AuditLogs() {
  const [rows, setRows] = useState<AuditRow[]>([])
  const [total, setTotal] = useState(0)
  const [page, setPage] = useState(1)
  const [loading, setLoading] = useState(false)
  const [kw, setKw] = useState('')

  const load = () => {
    setLoading(true)
    client
      .get<ApiResp<{ total: number; items: AuditRow[] }>>('/audit-logs', {
        params: { page, page_size: 50, username: kw || undefined },
        headers: { Authorization: `Bearer ${getToken()}` },
      })
      .then((r) => { setRows(r.data.data?.items ?? []); setTotal(r.data.data?.total ?? 0) })
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }

  useEffect(load, [page, kw])

  return (
    <Card
      title="审计日志（写操作全程留痕，仅管理员）"
      extra={<Input.Search placeholder="按操作人筛选" style={{ width: 200 }} onSearch={(v) => { setPage(1); setKw(v) }} allowClear />}
    >
      <Table
        rowKey="id" loading={loading} size="small" dataSource={rows} columns={columns}
        pagination={{ current: page, pageSize: 50, total, onChange: setPage, showTotal: (t) => `共 ${t} 条` }}
      />
    </Card>
  )
}