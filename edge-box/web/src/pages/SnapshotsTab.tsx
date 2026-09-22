import { useCallback, useEffect, useState } from 'react'
import { Button, Card, Empty, Image, Modal, Space, Table, Tag, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import dayjs from 'dayjs'
import { fetchSnapshots } from '../api'
import type { Snapshot } from '../types'

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
    width: 90,
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
    width: 96,
    render: (v: number, r) => (r.customer_id != null ? v.toFixed(3) : '-'),
  },
  { title: '方向', dataIndex: 'direction', width: 70, render: (v: number) => DIR_TEXT[v] ?? '-' },
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
  const [loading, setLoading] = useState(false)
  const [preview, setPreview] = useState<Snapshot | null>(null)

  const load = useCallback(() => {
    setLoading(true)
    fetchSnapshots(20)
      .then((list) => setRows(list ?? []))
      .catch(() => message.warning('抓拍记录获取失败'))
      .finally(() => setLoading(false))
  }, [])

  useEffect(() => {
    load()
    const timer = window.setInterval(load, 10000)
    return () => window.clearInterval(timer)
  }, [load])

  return (
    <Card
      size="small"
      title="最近抓拍记录"
      extra={
        <Space>
          <span style={{ color: '#999', fontSize: 12 }}>每 10 秒自动刷新</span>
          <Button size="small" onClick={load} loading={loading}>刷新</Button>
        </Space>
      }
    >
      {rows.length === 0 && !loading ? (
        <Empty description="暂无抓拍记录（本机无抓拍/内存模式运行）" />
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
        title={`抓拍详情 · ${preview ? dayjs.unix(preview.created_at).format('YYYY-MM-DD HH:mm:ss') : ''}`}
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