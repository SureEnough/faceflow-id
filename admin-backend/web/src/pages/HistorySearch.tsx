import { useState } from 'react'
import { Alert, Button, Card, Descriptions, Input, Space, Table, Tag, Typography, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import { historySearch } from '../api'
import { errMsg } from '../api/client'
import { randomFeatureBase64 } from '../api/feature'
import type { MatchRecord } from '../api/types'

const columns: ColumnsType<MatchRecord> = [
  { title: '记录ID', dataIndex: 'log_id', width: 90 },
  { title: '相似度', dataIndex: 'similarity', render: (v: number) => v.toFixed(4) },
  {
    title: '方向', dataIndex: 'direction',
    render: (v?: number) => (v === 0 ? <Tag color="green">进</Tag> : v === 1 ? <Tag color="orange">出</Tag> : '-'),
  },
  { title: '摄像头', dataIndex: 'camera_id' },
  { title: '识别时间', dataIndex: 'created_at' },
]

export default function HistorySearch() {
  const [feature, setFeature] = useState('')
  const [threshold, setThreshold] = useState('0.40')
  const [loading, setLoading] = useState(false)
  const [result, setResult] = useState<{
    total_visits: number
    visit_days: number
    first_visit_at: string
    last_visit_at: string
    matched_records: MatchRecord[]
  } | null>(null)
  const [error, setError] = useState('')

  const search = async () => {
    if (!feature) {
      message.warning('请先粘贴人脸特征（base64）')
      return
    }
    setLoading(true); setError('')
    try {
      const r = await historySearch({ face_feature: feature, similarity_threshold: Number(threshold) || 0.4 })
      setResult(r)
    } catch (e) {
      setError(errMsg(e))
    } finally {
      setLoading(false)
    }
  }

  return (
    <Card title="历史来访回查（顾客录入后自动触发，也可在此手动查询）">
      <Space direction="vertical" size={12} style={{ width: '100%' }}>
        <Space.Compact style={{ width: '100%' }}>
          <Input.TextArea
            rows={2} placeholder="粘贴人脸特征 base64（512 维 float32 → 2048 字节 → base64）"
            value={feature} onChange={(e) => setFeature(e.target.value)}
          />
          <Button onClick={() => { setFeature(randomFeatureBase64(42)); message.info('已生成测试特征（seed=42）') }}>
            🎲 生成测试特征
          </Button>
        </Space.Compact>
        <Space>
          相似度阈值：
          <Input
            style={{ width: 90 }} value={threshold}
            onChange={(e) => setThreshold(e.target.value)} placeholder="0.40"
          />
          <Button type="primary" loading={loading} onClick={search}>开始回查</Button>
        </Space>
        {error && <Alert type="error" showIcon message={error} />}
        {result && (
          <Card size="small">
            <Descriptions column={4} size="small" bordered>
              <Descriptions.Item label="来访次数"><b style={{ color: '#cf1322' }}>{result.total_visits}</b></Descriptions.Item>
              <Descriptions.Item label="来访天数">{result.visit_days}</Descriptions.Item>
              <Descriptions.Item label="首次到访">{result.first_visit_at || '-'}</Descriptions.Item>
              <Descriptions.Item label="最近到访">{result.last_visit_at || '-'}</Descriptions.Item>
            </Descriptions>
            <Typography.Text type="secondary">匹配明细（按相似度降序，最多 Top-K 条）：</Typography.Text>
            <Table
              style={{ marginTop: 12 }} rowKey="log_id" size="small" columns={columns}
              dataSource={result.matched_records} pagination={false}
            />
          </Card>
        )}
      </Space>
    </Card>
  )
}