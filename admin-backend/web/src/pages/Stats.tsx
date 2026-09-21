import { useEffect, useMemo, useState } from 'react'
import { Card, DatePicker, Radio, Space, Table, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import type { EChartsOption } from 'echarts'
import dayjs, { type Dayjs } from 'dayjs'
import { fetchFlowStats } from '../api'
import { errMsg } from '../api/client'
import type { FlowRow } from '../api/types'
import EChart from '../components/EChart'

const columns: ColumnsType<FlowRow> = [
  { title: '时间', dataIndex: 'bucket' },
  { title: '进店', dataIndex: 'in', render: (v: number) => v },
  { title: '出店', dataIndex: 'out', render: (v: number) => v },
]

const GRAN_TEXT: Record<string, string> = { hour: '时', day: '日', week: '周', month: '月' }

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

  const option = useMemo<EChartsOption>(() => ({
    tooltip: { trigger: 'axis' },
    legend: { data: ['进店', '出店'] },
    grid: { left: 40, right: 20, top: 40, bottom: 30 },
    xAxis: { type: 'category', data: rows.map((r) => r.bucket), axisLabel: { rotate: gran === 'hour' ? 40 : 0 } },
    yAxis: { type: 'value', minInterval: 1 },
    series: [
      {
        name: '进店', type: 'bar', data: rows.map((r) => r.in),
        itemStyle: { color: '#1677ff' }, barMaxWidth: 28,
      },
      {
        name: '出店', type: 'bar', data: rows.map((r) => r.out),
        itemStyle: { color: '#ffa940' }, barMaxWidth: 28,
      },
    ],
  }), [rows, gran])

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
          <DatePicker.RangePicker value={range} onChange={(v) => v && setRange([v[0]!, v[1]!])} />
        </Space>
      }
    >
      {loading ? (
        <div style={{ height: 320, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>加载中…</div>
      ) : rows.length === 0 ? (
        <div style={{ height: 320, display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#999' }}>
          该时间范围暂无客流数据（可运行 python3 scripts/demo_seed.py 造演示数据）
        </div>
      ) : (
        <>
          <EChart option={option} height={320} />
          <Table
            rowKey="bucket" loading={loading} size="small" dataSource={rows} columns={columns}
            pagination={false} style={{ marginTop: 16 }}
            summary={() => (
              <Table.Summary.Row>
                <Table.Summary.Cell index={0}><b>合计（按{GRAN_TEXT[gran]}）</b></Table.Summary.Cell>
                <Table.Summary.Cell index={1}><b>{total.in}</b></Table.Summary.Cell>
                <Table.Summary.Cell index={2}><b>{total.out}</b></Table.Summary.Cell>
              </Table.Summary.Row>
            )}
          />
        </>
      )}
    </Card>
  )
}