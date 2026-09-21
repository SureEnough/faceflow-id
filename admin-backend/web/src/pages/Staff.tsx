import { useEffect, useState } from 'react'
import { Card, DatePicker, Table, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import dayjs, { type Dayjs } from 'dayjs'
import { fetchStaffStats } from '../api'
import { errMsg } from '../api/client'
import type { StaffRow } from '../api/types'

const columns: ColumnsType<StaffRow> = [
  { title: '工号', dataIndex: 'staff_no' },
  { title: '部门', dataIndex: 'department' },
  { title: '进', dataIndex: 'in' },
  { title: '出', dataIndex: 'out' },
  { title: '最近进入', dataIndex: 'last_in' },
]

export default function Staff() {
  const [rows, setRows] = useState<StaffRow[]>([])
  const [range, setRange] = useState<[Dayjs, Dayjs]>([dayjs().subtract(7, 'day'), dayjs()])
  const [loading, setLoading] = useState(false)

  useEffect(() => {
    setLoading(true)
    fetchStaffStats({
      start_at: range[0].startOf('day').toISOString(),
      end_at: range[1].endOf('day').toISOString(),
    })
      .then((r) => setRows(r.items))
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }, [range])

  return (
    <Card
      title="内部人员通行记录"
      extra={
        <DatePicker.RangePicker
          value={range}
          onChange={(v) => v && setRange([v[0]!, v[1]!])}
        />
      }
    >
      <Table rowKey="customer_id" loading={loading} size="small" dataSource={rows} columns={columns} pagination={false} />
    </Card>
  )
}