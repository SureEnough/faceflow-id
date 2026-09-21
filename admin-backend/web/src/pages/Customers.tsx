import { useEffect, useState } from 'react'
import { Button, Card, Form, Input, Modal, Radio, Select, Space, Table, Tag, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import dayjs from 'dayjs'
import { createCustomer, fetchCustomers } from '../api'
import { errMsg } from '../api/client'
import type { Customer } from '../api/types'

const columns: ColumnsType<Customer> = [
  { title: 'ID', dataIndex: 'id', width: 70 },
  {
    title: '类型', dataIndex: 'person_type', width: 90,
    render: (v: number) => (v === 1 ? <Tag color="geekblue">内部人员</Tag> : <Tag color="green">顾客</Tag>),
  },
  { title: '姓名', dataIndex: 'name' },
  { title: '身份证号', dataIndex: 'id_card_no', ellipsis: true },
  { title: '工号', dataIndex: 'staff_no' },
  { title: '部门', dataIndex: 'department' },
  { title: '状态', dataIndex: 'status', render: (v: number) => (v === 0 ? '正常' : v === 1 ? '黑名单' : '注销/离职') },
  {
    title: '更新时间', dataIndex: 'updated_at', width: 160,
    render: (v: number) => (v ? dayjs.unix(v).format('YYYY-MM-DD HH:mm') : '-'),
  },
]

export default function Customers() {
  const [data, setData] = useState<Customer[]>([])
  const [total, setTotal] = useState(0)
  const [page, setPage] = useState(1)
  const [personType, setPersonType] = useState<number | undefined>()
  const [loading, setLoading] = useState(false)
  const [open, setOpen] = useState(false)
  const [form] = Form.useForm()

  const load = () => {
    setLoading(true)
    fetchCustomers({ page, page_size: 20, person_type: personType })
      .then((r) => { setData(r.items); setTotal(r.total) })
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }

  useEffect(load, [page, personType])

  const submit = async () => {
    const values = await form.validateFields()
    try {
      const r = await createCustomer(values as Record<string, unknown>)
      if (r.history) {
        message.success(
          `录入成功，历史来访 ${r.history.total_visits} 次 / ${r.history.visit_days} 天`,
        )
      } else {
        message.success('内部人员录入成功')
      }
      setOpen(false); form.resetFields(); load()
    } catch (e) {
      message.error(errMsg(e))
    }
  }

  return (
    <Card
      title="人员库"
      extra={
        <Space>
          <Select
            style={{ width: 140 }} placeholder="人员类型"
            allowClear
            value={personType}
            onChange={(v) => { setPage(1); setPersonType(v) }}
            options={[{ value: 0, label: '顾客' }, { value: 1, label: '内部人员' }]}
          />
          <Button type="primary" onClick={() => setOpen(true)}>新增人员</Button>
        </Space>
      }
    >
      <Table
        rowKey="id" loading={loading} dataSource={data} columns={columns}
        pagination={{ current: page, pageSize: 20, total, onChange: setPage, showTotal: (t) => `共 ${t} 条` }}
      />
      <Modal title="新增人员档案" open={open} onOk={submit} onCancel={() => setOpen(false)} destroyOnClose>
        <Form form={form} layout="vertical">
          <Form.Item name="person_type" label="类型" initialValue={0} rules={[{ required: true }]}>
            <Radio.Group options={[{ value: 0, label: '顾客' }, { value: 1, label: '内部人员' }]} />
          </Form.Item>
          <Form.Item name="name" label="姓名" rules={[{ required: true }]}>
            <Input placeholder="张三" />
          </Form.Item>
          <Form.Item noStyle shouldUpdate>
            {({ getFieldValue }) =>
              getFieldValue('person_type') === 0 ? (
                <>
                  <Form.Item name="id_card_no" label="身份证号" rules={[{ required: true }]}>
                    <Input placeholder="18 位身份证号" maxLength={18} />
                  </Form.Item>
                  <Form.Item name="birth_date" label="出生日期">
                    <Input placeholder="1990-01-01" />
                  </Form.Item>
                </>
              ) : (
                <>
                  <Form.Item name="staff_no" label="工号" rules={[{ required: true }]}>
                    <Input placeholder="E1024" />
                  </Form.Item>
                  <Form.Item name="department" label="部门">
                    <Input placeholder="运营部" />
                  </Form.Item>
                </>
              )
            }
          </Form.Item>
          <Form.Item name="face_feature" label="人脸特征 (base64, 512×float32)" extra="骨架阶段可留空或粘贴测试特征">
            <Input.TextArea rows={2} placeholder="base64 编码的 512 维特征" />
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  )
}