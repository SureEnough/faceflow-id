import { useEffect, useState } from 'react'
import { Alert, Card, Col, Row, Space, Statistic, Typography } from 'antd'
import { get, errMsg } from '../api/client'
import { fetchDeviceTree } from '../api'

export default function Dashboard() {
  const [health, setHealth] = useState<string>('')
  const [online, setOnline] = useState(0)
  const [total, setTotal] = useState(0)
  const [error, setError] = useState('')

  useEffect(() => {
    get<{ status: string; time: string }>('/health').then((d) => setHealth(d.status)).catch(() => setHealth('down'))
    fetchDeviceTree()
      .then((items) => {
        setTotal(items.length)
        setOnline(items.filter((i) => i.status === 1).length)
      })
      .catch((e) => setError(errMsg(e)))
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
      <Card size="small">
        <Typography.Text type="secondary">
          系统由三端组成：边缘盒子人脸识别端 / 顾客录入电脑端 / 管理后台。本后台覆盖：设备树、人员库、客流统计、员工通行、历史来访回查。
        </Typography.Text>
      </Card>
    </Space>
  )
}