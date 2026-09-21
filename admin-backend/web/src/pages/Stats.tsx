import { useEffect, useState } from 'react'
import { Card, DatePicker, Radio, Space, Table, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import dayjs, { type Dayjs } from 'dayjs'
import { fetchFlowStats } from '../api'
import { errMsg } from '../api/client'
import type { FlowRow } from '../api/types'

const columns: ColumnsType<FlowRow> = [
  { title: '时间', dataIndex: 'bucket' },
  { title: '进店', dataIndex: 'in', render: (v: number) => v },
  { title: '出店', dataIndex: 'out', render: (v: number) => v },
]

export default function Stats() {
  const [rows, setRows] = useState<FlowRow[]>([])
  const [total, setTotal] = useState({ in: 0, out: 0 })
  const [gran, setGran] = useState('day')
  const [range, setRange] = useState<[Dayjs, Dayjs]>([dayjs().subtract(7, 'day'), dayjs()])
  const [loading, setLoading] = useState(false)

  useEffect(() => {
    setLoading(true)
    fetchFlowStats({
      granularity: gran,
      start_at: range[0].startOf('day').toISOString(),
      end_at: range[1].endOf('day').toISOString(),
    })
      .then((r) => { setRows(r.items); setTotal(r.total) })
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }, [gran, range])

  return (
    <Card
      title="客流统计（顾客口径，内部人员已排除）"
      extra={
        <Space>
          <Radio.Group value={gran} onChange={(e) => setGran(e.target.value)} optionType="button">
            <Radio.Button value="hour">小时</Radio.Button>
            <Radio.Button value="day">日</Radio.Button>
            <Radio.Button value="week">周</Radio.Button>
            <Radio.Button value="month">月</Radio.Button>
          </Radio.Group>
          <DatePicker.RangePicker
            value={range}
            onChange={(v) => v && setRange([v[0]!, v[1]!])}
          />
        </Space>
      }
    >
      <Table
        rowKey="bucket" loading={loading} size="small" dataSource={rows} columns={columns}
        pagination={false}
        summary={() => (
          <Table.Summary.Row>
            <Table.Summary.Cell index={0}><b>合计</b></Table.Summary.Cell>
            <Table.Summary.Cell index={1}><b>{total.in}</b></Table.Summary.Cell>
            <Table.Summary.Cell index={2}><b>{total.out}</b></Table.Summary.Cell>
          </Table.Summary.Row>
        )}
      />
    </Card>
  )
}