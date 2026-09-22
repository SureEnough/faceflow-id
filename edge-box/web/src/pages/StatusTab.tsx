import { useEffect, useState } from 'react'
import { Card, Col, Row, Statistic, Table, Tag, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import { fetchStatus } from '../api'
import type { CameraStatus, EdgeBoxStatus } from '../types'

function fmtDuration(sec: number): string {
  if (sec < 0) return '-'
  const d = Math.floor(sec / 86400)
  const h = Math.floor((sec % 86400) / 3600)
  const m = Math.floor((sec % 3600) / 60)
  const s = sec % 60
  return d > 0 ? `${d}天${h}时` : `${h}时${m}分${s}秒`
}

const columns: ColumnsType<CameraStatus> = [
  { title: '相机', dataIndex: 'camera_id' },
  {
    title: '视频源',
    dataIndex: 'opened',
    render: (opened: boolean) => (opened ? <Tag color="green">已打开</Tag> : <Tag color="red">未打开</Tag>),
  },
  {
    title: '最近取帧',
    dataIndex: 'last_frame_ok',
    render: (ok: boolean) => (ok ? <Tag color="green">正常</Tag> : <Tag color="orange">失败</Tag>),
  },
  { title: '帧数', dataIndex: 'frames' },
  { title: '客流进', dataIndex: 'flow_in' },
  { title: '客流出', dataIndex: 'flow_out' },
]

export default function StatusTab() {
  const [status, setStatus] = useState<EdgeBoxStatus | null>(null)

  useEffect(() => {
    let alive = true
    const load = () =>
      fetchStatus()
        .then((s) => alive && setStatus(s))
        .catch(() => alive && message.warning('状态获取失败'))
    load()
    const timer = window.setInterval(load, 5000)
    return () => {
      alive = false
      window.clearInterval(timer)
    }
  }, [])

  return (
    <Row gutter={[16, 16]}>
      <Col span={4}>
        <Card size="small">
          <Statistic title="设备 ID" value={status?.device_id ?? '-'} />
        </Card>
      </Col>
      <Col span={5}>
        <Card size="small">
          <Statistic title="推理后端" value={status?.backend ?? '-'} />
        </Card>
      </Col>
      <Col span={5}>
        <Card size="small">
          <Statistic title="版本" value={status?.version ?? '-'} />
        </Card>
      </Col>
      <Col span={5}>
        <Card size="small">
          <Statistic title="运行时长" value={status ? fmtDuration(status.uptime_s) : '-'} />
        </Card>
      </Col>
      <Col span={5}>
        <Card size="small">
          <Statistic title="人员库版本" value={status?.sync_version ?? '-'} />
        </Card>
      </Col>
      <Col span={24}>
        <Card size="small" title="摄像头">
          <Table<CameraStatus>
            rowKey="camera_id"
            size="small"
            dataSource={status?.cameras ?? []}
            columns={columns}
            pagination={false}
          />
        </Card>
      </Col>
    </Row>
  )
}