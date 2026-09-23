import { useCallback, useEffect, useState } from 'react'
import {
  Button, Card, DatePicker, Empty, Image, InputNumber, Modal, Select, Space, Table, Tag, message,
} from 'antd'
import type { ColumnsType } from 'antd/es/table'
import dayjs, { type Dayjs } from 'dayjs'
import { fetchConfig, fetchSnapshots, type SnapshotQuery } from '../api'
import type { EdgeBoxConfig, Snapshot } from '../types'

const DIR_TEXT: Record<number, string> = { 0: '进', 1: '出' }
const PERSON_TEXT: Record<number, string> = { 0: '顾客', 1: '员工' }
const PERSON_COLOR: Record<number, string> = { 0: 'blue', 1: 'purple' }

const columns: ColumnsType<Snapshot> = [
  {
    title: '抓拍',
    dataIndex: 'snapshot',
    width: 84,
    render: (b64: string, r) =>
      b64 ? (
        <Image
          width={64}
          height={48}
          src={`data:${r.snapshot_mime || 'image/jpeg'};base64,${b64}`}
          style={{ objectFit: 'cover', borderRadius: 4, background: '#f5f5f5' }}
          preview={{ mask: '查看' }}
          placeholder={<div style={{ width: 64, height: 48, background: '#f5f5f5' }} />}
        />
      ) : (
        <div style={{ width: 64, height: 48, background: '#f5f5f5', borderRadius: 4, display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#bbb', fontSize: 12 }}>无图</div>
      ),
  },
  {
    title: '类型',
    dataIndex: 'person_type',
    width: 100,
    render: (v: number, r) =>
      r.customer_id != null ? (
        <Space size={4}>
          <Tag color={PERSON_COLOR[v]}>{PERSON_TEXT[v] ?? '未知'}</Tag>
          <span style={{ fontSize: 12, color: '#999' }}>#{r.customer_id}</span>
        </Space>
      ) : (
        <Tag>匿名</Tag>
      ),
  },
  {
    title: '相似度',
    dataIndex: 'similarity',
    width: 90,
    render: (v: number, r) => (r.customer_id != null ? v.toFixed(3) : '-'),
  },
  { title: '方向', dataIndex: 'direction', width: 66, render: (v: number) => DIR_TEXT[v] ?? '-' },
  { title: '相机', dataIndex: 'camera_id', width: 110 },
  {
    title: '轨迹 ID',
    dataIndex: 'track_id',
    ellipsis: true,
    render: (v: string) => <span style={{ fontFamily: 'monospace', fontSize: 12 }}>{v}</span>,
  },
  {
    title: '时间',
    dataIndex: 'created_at',
    width: 160,
    render: (v: number) => dayjs.unix(v).format('YYYY-MM-DD HH:mm:ss'),
  },
]

export default function SnapshotsTab() {
  const [rows, setRows] = useState<Snapshot[]>([])
  const [cameras, setCameras] = useState<string[]>([])
  const [loading, setLoading] = useState(false)
  const [preview, setPreview] = useState<Snapshot | null>(null)

  // 筛选条件（undefined 表示不限制）
  const [cameraId, setCameraId] = useState<string | undefined>()
  const [identified, setIdentified] = useState<string>('-1')
  const [personType, setPersonType] = useState<string>('-1')
  const [direction, setDirection] = useState<string>('-1')
  const [minSim, setMinSim] = useState<number | undefined>()
  const [range, setRange] = useState<{ start: Dayjs | null; end: Dayjs | null }>({ start: null, end: null })

  // 相机选项：从配置读取
  useEffect(() => {
    fetchConfig()
      .then((cfg: EdgeBoxConfig) => setCameras((cfg.cameras ?? []).map((c) => c.camera_id).filter(Boolean)))
      .catch(() => setCameras([]))
  }, [])

  const buildQuery = useCallback((): SnapshotQuery => {
    const q: SnapshotQuery = { limit: 20 }
    if (cameraId) q.camera_id = cameraId
    if (identified !== '-1') q.identified = Number(identified)
    if (personType !== '-1') q.person_type = Number(personType)
    if (direction !== '-1') q.direction = Number(direction)
    if (minSim != null) q.min_similarity = minSim
    if (range.start) q.start_at = range.start.startOf('day').unix()
    if (range.end) q.end_at = range.end.endOf('day').unix()
    return q
  }, [cameraId, identified, personType, direction, minSim, range])

  const load = useCallback(() => {
    setLoading(true)
    fetchSnapshots(buildQuery())
      .then((list) => setRows(list ?? []))
      .catch(() => message.warning('识别记录获取失败'))
      .finally(() => setLoading(false))
  }, [buildQuery])

  useEffect(() => {
    load()
    const timer = window.setInterval(load, 10000)
    return () => window.clearInterval(timer)
  }, [load])

  return (
    <Card
      size="small"
      title="识别记录"
      extra={
        <Space>
          <span style={{ color: '#999', fontSize: 12 }}>每 10 秒自动刷新</span>
          <Button size="small" onClick={load} loading={loading}>查询</Button>
        </Space>
      }
    >
      <Space wrap style={{ marginBottom: 12 }}>
        <Select
          style={{ width: 130 }} placeholder="相机" allowClear value={cameraId}
          onChange={(v) => setCameraId(v)}
          options={cameras.map((c) => ({ value: c, label: c }))}
        />
        <Select
          style={{ width: 120 }} value={identified} onChange={setIdentified}
          options={[
            { value: '-1', label: '全部类型' },
            { value: '1', label: '已识别' },
            { value: '0', label: '匿名' },
          ]}
        />
        <Select
          style={{ width: 110 }} value={personType} onChange={setPersonType}
          options={[
            { value: '-1', label: '全部人员' },
            { value: '0', label: '顾客' },
            { value: '1', label: '员工' },
          ]}
        />
        <Select
          style={{ width: 100 }} value={direction} onChange={setDirection}
          options={[
            { value: '-1', label: '全部方向' },
            { value: '0', label: '进' },
            { value: '1', label: '出' },
          ]}
        />
        <span>
          最低相似度
          <InputNumber
            style={{ width: 90, marginLeft: 6 }} min={0} max={1} step={0.01} placeholder="0.40"
            value={minSim} onChange={(v) => setMinSim(v ?? undefined)}
          />
        </span>
        <DatePicker.RangePicker
          value={[range.start, range.end]}
          onChange={(v) => setRange({ start: v?.[0] ?? null, end: v?.[1] ?? null })}
        />
      </Space>
      {rows.length === 0 && !loading ? (
        <Empty description="暂无识别记录（本机无记录/内存模式运行）" />
      ) : (
        <Table<Snapshot>
          rowKey={(r) => `${r.track_id}-${r.camera_id}-${r.created_at}`}
          size="small"
          loading={loading}
          dataSource={rows}
          columns={columns}
          pagination={{ pageSize: 10, showSizeChanger: false }}
          onRow={(r) => ({
            onClick: () => setPreview(r),
            style: { cursor: r.snapshot ? 'pointer' : 'default' },
          })}
        />
      )}

      <Modal
        open={!!preview}
        title={`识别详情 · ${preview ? dayjs.unix(preview.created_at).format('YYYY-MM-DD HH:mm:ss') : ''}`}
        footer={null}
        onCancel={() => setPreview(null)}
      >
        {preview && (
          <div style={{ textAlign: 'center' }}>
            {preview.snapshot ? (
              <img
                src={`data:${preview.snapshot_mime || 'image/jpeg'};base64,${preview.snapshot}`}
                alt="抓拍"
                style={{ maxWidth: '100%', maxHeight: 420, borderRadius: 6 }}
              />
            ) : (
              <Empty description="该记录无抓拍图" />
            )}
            <Space style={{ marginTop: 12 }} size={8} wrap>
              <Tag>{preview.camera_id}</Tag>
              {preview.customer_id != null ? (
                <Tag color={PERSON_COLOR[preview.person_type]}>{PERSON_TEXT[preview.person_type]} #{preview.customer_id}</Tag>
              ) : (
                <Tag>匿名</Tag>
              )}
              {preview.customer_id != null && <Tag>相似度 {preview.similarity.toFixed(3)}</Tag>}
              <Tag>方向 {DIR_TEXT[preview.direction] ?? '-'}</Tag>
            </Space>
            <div style={{ marginTop: 8, color: '#999', fontSize: 12, wordBreak: 'break-all' }}>
              轨迹 ID：{preview.track_id}
            </div>
          </div>
        )}
      </Modal>
    </Card>
  )
}