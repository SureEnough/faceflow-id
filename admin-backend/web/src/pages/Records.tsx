import { useEffect, useState } from 'react'
import { Card, DatePicker, Image, Input, Select, Space, Table, Tabs, Tag, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import type { Dayjs } from 'dayjs'
import { fetchRecognitionRecords, fetchVerifyRecords } from '../api'
import { errMsg } from '../api/client'
import type { RecognitionRecord, VerifyRecord } from '../api/types'

// ---------- 识别记录 ----------

const RESULT_TEXT: Record<number, { t: string; c: string }> = {
  0: { t: '待定', c: 'default' },
  1: { t: '通过', c: 'green' },
  2: { t: '不通过', c: 'red' },
}

function snapshotSrc(r: RecognitionRecord): string | undefined {
  if (r.snapshot_url) return r.snapshot_url
  if (r.snapshot && r.snapshot_mime) return `data:${r.snapshot_mime};base64,${r.snapshot}`
  return undefined
}

function RecognitionTab() {
  const [items, setItems] = useState<RecognitionRecord[]>([])
  const [total, setTotal] = useState(0)
  const [page, setPage] = useState(1)
  const [deviceId, setDeviceId] = useState<string>('')
  const [direction, setDirection] = useState<string>('')
  const [range, setRange] = useState<{ start: Dayjs | null; end: Dayjs | null }>({ start: null, end: null })
  const [loading, setLoading] = useState(false)

  const load = () => {
    setLoading(true)
    const params: Record<string, unknown> = { page, page_size: 20 }
    if (deviceId) params.device_id = deviceId
    if (direction !== '') params.direction = direction
    if (range.start) params.start_at = range.start.toISOString()
    if (range.end) params.end_at = range.end.endOf('day').toISOString()
    fetchRecognitionRecords(params)
      .then((r) => { setItems(r.items); setTotal(r.total) })
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }

  useEffect(load, [page, deviceId, direction, range])

  const columns: ColumnsType<RecognitionRecord> = [
    { title: 'ID', dataIndex: 'id', width: 80 },
    { title: '设备', dataIndex: 'device_id', width: 80 },
    { title: '摄像头', dataIndex: 'camera_id', width: 100 },
    {
      title: '方向', dataIndex: 'direction', width: 80,
      render: (v: number) => (v === 0 ? <Tag color="green">进</Tag> : v === 1 ? <Tag color="orange">出</Tag> : '-'),
    },
    { title: '相似度', dataIndex: 'similarity', width: 90, render: (v: number) => v.toFixed(4) },
    {
      title: '人员', dataIndex: 'customer_name', width: 120,
      render: (v: string | undefined, r) => (v ? `${v}${r.customer_id ? `(#${r.customer_id})` : ''}` : <Tag>匿名</Tag>),
    },
    { title: '轨迹ID', dataIndex: 'track_id', ellipsis: true },
    { title: '快照', dataIndex: 'id', width: 90, render: (_, r) => (snapshotSrc(r) ? <Image src={snapshotSrc(r)} width={56} height={40} style={{ objectFit: 'cover' }} /> : '-') },
    { title: '识别时间', dataIndex: 'created_at', width: 170 },
  ]

  return (
    <>
      <Space wrap style={{ marginBottom: 12 }}>
        <Input
          style={{ width: 120 }} placeholder="设备 ID" value={deviceId}
          onChange={(e) => { setPage(1); setDeviceId(e.target.value) }}
        />
        <Select
          style={{ width: 120 }} placeholder="方向" allowClear value={direction}
          onChange={(v) => { setPage(1); setDirection(v ?? '') }}
          options={[{ value: '0', label: '进' }, { value: '1', label: '出' }]}
        />
        <DatePicker.RangePicker
          value={[range.start, range.end]}
          onChange={(v) => { setPage(1); setRange({ start: v?.[0] ?? null, end: v?.[1] ?? null }) }}
        />
      </Space>
      <Table
        rowKey="id" loading={loading} dataSource={items} columns={columns} size="small"
        pagination={{ current: page, pageSize: 20, total, onChange: setPage, showTotal: (t) => `共 ${t} 条` }}
      />
    </>
  )
}

// ---------- 核验记录 ----------

function VerifyTab() {
  const [items, setItems] = useState<VerifyRecord[]>([])
  const [total, setTotal] = useState(0)
  const [page, setPage] = useState(1)
  const [result, setResult] = useState<string>('')
  const [range, setRange] = useState<{ start: Dayjs | null; end: Dayjs | null }>({ start: null, end: null })
  const [loading, setLoading] = useState(false)

  const load = () => {
    setLoading(true)
    const params: Record<string, unknown> = { page, page_size: 20 }
    if (result !== '') params.verify_result = result
    if (range.start) params.start_at = range.start.toISOString()
    if (range.end) params.end_at = range.end.endOf('day').toISOString()
    fetchVerifyRecords(params)
      .then((r) => { setItems(r.items); setTotal(r.total) })
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }

  useEffect(load, [page, result, range])

  const columns: ColumnsType<VerifyRecord> = [
    { title: 'ID', dataIndex: 'id', width: 80 },
    { title: '顾客ID', dataIndex: 'customer_id', width: 90 },
    { title: '结果', dataIndex: 'verify_result', width: 90, render: (v: number) => <Tag color={RESULT_TEXT[v]?.c ?? 'default'}>{RESULT_TEXT[v]?.t ?? v}</Tag> },
    { title: '相似度', dataIndex: 'similarity', width: 90, render: (v: number) => v.toFixed(4) },
    { title: '活体分', dataIndex: 'liveness_score', width: 90, render: (v: number) => v.toFixed(3) },
    { title: '设备', dataIndex: 'device_id', width: 80 },
    { title: '操作员', dataIndex: 'operator' },
    {
      title: '现场照', dataIndex: 'id', width: 90,
      render: (_, r) => (
        r.live_photo_url
          ? <Image src={r.live_photo_url} width={56} height={40} style={{ objectFit: 'cover' }} />
          : r.live_photo
            ? <Image src={`data:image/jpeg;base64,${r.live_photo}`} width={56} height={40} style={{ objectFit: 'cover' }} />
            : '-'
      ),
    },
    { title: '核验时间', dataIndex: 'created_at', width: 170 },
  ]

  return (
    <>
      <Space wrap style={{ marginBottom: 12 }}>
        <Select
          style={{ width: 130 }} placeholder="核验结果" allowClear value={result}
          onChange={(v) => { setPage(1); setResult(v ?? '') }}
          options={[
            { value: '1', label: '通过' }, { value: '2', label: '不通过' }, { value: '0', label: '待定' },
          ]}
        />
        <DatePicker.RangePicker
          value={[range.start, range.end]}
          onChange={(v) => { setPage(1); setRange({ start: v?.[0] ?? null, end: v?.[1] ?? null }) }}
        />
      </Space>
      <Table
        rowKey="id" loading={loading} dataSource={items} columns={columns} size="small"
        pagination={{ current: page, pageSize: 20, total, onChange: setPage, showTotal: (t) => `共 ${t} 条` }}
      />
    </>
  )
}

// ---------- 页面 ----------

export default function Records() {
  return (
    <Card title="记录查询">
      <Tabs
        items={[
          { key: 'recognition', label: '识别记录', children: <RecognitionTab /> },
          { key: 'verify', label: '人证核验记录', children: <VerifyTab /> },
        ]}
      />
    </Card>
  )
}