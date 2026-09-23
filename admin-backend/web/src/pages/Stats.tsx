import { useEffect, useMemo, useState } from 'react'
import { Button, Card, DatePicker, Radio, Select, Space, Switch, Table, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import type { EChartsOption } from 'echarts'
import dayjs, { type Dayjs } from 'dayjs'
import { DownloadOutlined } from '@ant-design/icons'
import { downloadCsv, errMsg } from '../api/client'
import { fetchFlowStats, fetchStores } from '../api'
import type { FlowRow } from '../api/types'
import EChart from '../components/EChart'

const GRAN_TEXT: Record<string, string> = { hour: '时', day: '日', week: '周', month: '月' }

export default function Stats() {
  const [rows, setRows] = useState<FlowRow[]>([])
  const [total, setTotal] = useState<{ in: number; out: number; unique_persons?: number }>({ in: 0, out: 0 })
  const [gran, setGran] = useState('day')
  const [range, setRange] = useState<[Dayjs, Dayjs]>([dayjs().subtract(7, 'day'), dayjs()])
  const [stores, setStores] = useState<{ value: number; label: string }[]>([])
  const [storeIds, setStoreIds] = useState<number[]>([])
  const [groupBy, setGroupBy] = useState<'all' | 'camera'>('all')
  const [unique, setUnique] = useState(false)
  const [loading, setLoading] = useState(false)

  useEffect(() => {
    fetchStores().then((r) => {
      setStores(r.items.filter((i) => i.status === 1).map((i) => ({ value: i.id, label: i.name })))
    }).catch(() => setStores([]))
  }, [])

  const params = useMemo(() => {
    const p: Record<string, unknown> = {
      granularity: gran,
      start_at: range[0].startOf('day').toISOString(),
      end_at: range[1].endOf('day').toISOString(),
    }
    if (storeIds.length > 0) p.store_ids = storeIds
    if (groupBy === 'camera') p.group_by = 'camera'
    if (unique) p.unique = '1'
    return p
  }, [gran, range, storeIds, groupBy, unique])

  useEffect(() => {
    setLoading(true)
    fetchFlowStats(params)
      .then((r) => { setRows(r.items); setTotal(r.total) })
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }, [params])

  // 图表数据：汇总模式按时间桶；摄像头模式按摄像头聚合
  const chartData = useMemo(() => {
    if (groupBy === 'camera') {
      const byCam = new Map<string, { in: number; out: number }>()
      for (const r of rows) {
        const k = r.camera_id || '(无)'
        const cur = byCam.get(k) ?? { in: 0, out: 0 }
        cur.in += r.in; cur.out += r.out
        byCam.set(k, cur)
      }
      return { labels: Array.from(byCam.keys()), ins: Array.from(byCam.values()).map((v) => v.in), outs: Array.from(byCam.values()).map((v) => v.out) }
    }
    return { labels: rows.map((r) => r.bucket), ins: rows.map((r) => r.in), outs: rows.map((r) => r.out) }
  }, [rows, groupBy])

  const option = useMemo<EChartsOption>(() => ({
    tooltip: { trigger: 'axis' },
    legend: { data: ['进店', '出店'] },
    grid: { left: 40, right: 20, top: 40, bottom: 30 },
    xAxis: { type: 'category', data: chartData.labels, axisLabel: { rotate: gran === 'hour' ? 40 : 0 } },
    yAxis: { type: 'value', minInterval: 1 },
    series: [
      { name: '进店', type: 'bar', data: chartData.ins, itemStyle: { color: '#1677ff' }, barMaxWidth: 28 },
      { name: '出店', type: 'bar', data: chartData.outs, itemStyle: { color: '#ffa940' }, barMaxWidth: 28 },
    ],
  }), [chartData, gran])

  const columns: ColumnsType<FlowRow> = [
    ...(groupBy === 'camera' ? [{ title: '摄像头', dataIndex: 'camera_id', render: (v?: string) => v || '(无)' }] : []),
    { title: groupBy === 'camera' ? '时间' : '时间', dataIndex: 'bucket' },
    { title: '进店', dataIndex: 'in' },
    { title: '出店', dataIndex: 'out' },
  ]

  const doExport = () => {
    downloadCsv('/export/flow.csv', params, `flow-${gran}.csv`).catch((e) => message.error(errMsg(e)))
  }

  return (
    <Card
      title="客流统计（顾客口径，内部人员已排除）"
      extra={
        <Space wrap>
          <Select
            mode="multiple" allowClear placeholder="全部门店" style={{ minWidth: 160 }}
            value={storeIds} onChange={setStoreIds} options={stores}
          />
          <Radio.Group value={groupBy} onChange={(e) => setGroupBy(e.target.value)} optionType="button" size="small">
            <Radio.Button value="all">汇总</Radio.Button>
            <Radio.Button value="camera">按摄像头</Radio.Button>
          </Radio.Group>
          <Space size={4}>
            <Switch size="small" checked={unique} onChange={setUnique} />
            <span>识别人数</span>
          </Space>
          <Radio.Group value={gran} onChange={(e) => setGran(e.target.value)} optionType="button">
            <Radio.Button value="hour">小时</Radio.Button>
            <Radio.Button value="day">日</Radio.Button>
            <Radio.Button value="week">周</Radio.Button>
            <Radio.Button value="month">月</Radio.Button>
          </Radio.Group>
          <DatePicker.RangePicker value={range} onChange={(v) => v && setRange([v[0]!, v[1]!])} />
          <Button icon={<DownloadOutlined />} onClick={doExport}>导出 CSV</Button>
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
            rowKey={(r) => `${r.camera_id ?? ''}-${r.bucket}`} loading={loading} size="small"
            dataSource={rows} columns={columns} pagination={false} style={{ marginTop: 16 }}
            summary={() => (
              <Table.Summary.Row>
                <Table.Summary.Cell index={0} colSpan={groupBy === 'camera' ? 2 : 1}>
                  <b>合计（按{GRAN_TEXT[gran]}）</b>
                </Table.Summary.Cell>
                <Table.Summary.Cell index={1}><b>{total.in}</b></Table.Summary.Cell>
                <Table.Summary.Cell index={2}><b>{total.out}</b></Table.Summary.Cell>
                {unique && (
                  <Table.Summary.Cell index={3}>
                    <b>识别人数：{total.unique_persons ?? 0}</b>
                  </Table.Summary.Cell>
                )}
              </Table.Summary.Row>
            )}
          />
        </>
      )}
    </Card>
  )
}