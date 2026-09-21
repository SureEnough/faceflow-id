import { useEffect, useState } from 'react'
import { Alert, Card, Col, Row, Space, Statistic, Typography } from 'antd'
import dayjs from 'dayjs'
import { get, errMsg } from '../api/client'
import { fetchDeviceTree, fetchFlowStats } from '../api'
import type { FlowRow } from '../api/types'
import EChart from '../components/EChart'

export default function Dashboard() {
  const [health, setHealth] = useState<string>('')
  const [online, setOnline] = useState(0)
  const [total, setTotal] = useState(0)
  const [error, setError] = useState('')
  const [flow, setFlow] = useState<FlowRow[]>([])

  useEffect(() => {
    get<{ status: string; time: string }>('/health').then((d) => setHealth(d.status)).catch(() => setHealth('down'))
    fetchDeviceTree()
      .then((items) => {
        setTotal(items.length)
        setOnline(items.filter((i) => i.status === 1).length)
      })
      .catch((e) => setError(errMsg(e)))
    // 近 7 天客流趋势
    fetchFlowStats({
      granularity: 'day',
      start_at: dayjs().subtract(7, 'day').startOf('day').toISOString(),
      end_at: dayjs().endOf('day').toISOString(),
    })
      .then((r) => setFlow(r.items))
      .catch(() => setFlow([]))
  }, [])

  return (
    <Space direction="vertical" size={16} style={{ width: '100%' }}>
      {error && <Alert type="error" showIcon message="后端访问失败" description={error} />}
      <Alert
        type={health === 'up' ? 'success' : 'error'}
        showIcon
        message={`后端服务：${health === 'up' ? '在线' : '离线'}`}
        description="REST API 前缀 /api/v1；开发代理指向 http://127.0.0.1:8080"
      />
      <Row gutter={16}>
        <Col span={8}>
          <Card>
            <Statistic title="主设备（边缘盒/录入端）" value={total} suffix="台" />
          </Card>
        </Col>
        <Col span={8}>
          <Card>
            <Statistic title="在线设备" value={online} suffix="台" valueStyle={{ color: '#3f8600' }} />
          </Card>
        </Col>
        <Col span={8}>
          <Card>
            <Statistic title="设备类型" value={5} suffix="类" />
          </Card>
        </Col>
      </Row>
      <Card size="small" title="近 7 天客流趋势（顾客）">
        {flow.length > 0 ? (
          <EChart
            height={240}
            option={{
              tooltip: { trigger: 'axis' },
              legend: { data: ['进店', '出店'] },
              grid: { left: 40, right: 20, top: 30, bottom: 24 },
              xAxis: { type: 'category', data: flow.map((r) => r.bucket) },
              yAxis: { type: 'value', minInterval: 1 },
              series: [
                { name: '进店', type: 'line', smooth: true, data: flow.map((r) => r.in), areaStyle: { opacity: 0.15 } },
                { name: '出店', type: 'line', smooth: true, data: flow.map((r) => r.out), areaStyle: { opacity: 0.15 } },
              ],
            }}
          />
        ) : (
          <Typography.Text type="secondary">暂无数据（可运行 python3 admin-backend/scripts/demo_seed.py 造演示数据）</Typography.Text>
        )}
      </Card>
    </Space>
  )
}