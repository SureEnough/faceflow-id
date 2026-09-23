import { useEffect, useState } from 'react'
import { Button, Card, Form, Input, Modal, Popconfirm, Select, Space, Table, Tag, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import dayjs from 'dayjs'
import { createStore, deleteStore, fetchStores, updateStore } from '../api'
import { errMsg } from '../api/client'
import type { Store } from '../api/types'

interface Row extends Store { key: number }

export default function Stores() {
  const [data, setData] = useState<Row[]>([])
  const [loading, setLoading] = useState(false)
  const [open, setOpen] = useState(false)
  const [editing, setEditing] = useState<Row | null>(null)
  const [form] = Form.useForm()

  const load = () => {
    setLoading(true)
    fetchStores()
      .then((r) => setData(r.items.map((i) => ({ ...i, key: i.id }))))
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }

  useEffect(load, [])

  const openCreate = () => {
    setEditing(null)
    form.resetFields()
    setOpen(true)
  }

  const openEdit = (row: Row) => {
    setEditing(row)
    form.setFieldsValue({ name: row.name, address: row.address, status: row.status })
    setOpen(true)
  }

  const submit = async () => {
    const values = await form.validateFields()
    try {
      if (editing) {
        await updateStore(editing.id, values)
        message.success('已保存')
      } else {
        await createStore(values)
        message.success('门店已创建')
      }
      setOpen(false); load()
    } catch (e) {
      message.error(errMsg(e))
    }
  }

  const remove = async (row: Row) => {
    try {
      await deleteStore(row.id)
      message.success('已删除')
      load()
    } catch (e) {
      message.error(errMsg(e))
    }
  }

  const columns: ColumnsType<Row> = [
    { title: 'ID', dataIndex: 'id', width: 70 },
    { title: '名称', dataIndex: 'name' },
    { title: '地址', dataIndex: 'address', ellipsis: true },
    { title: '状态', dataIndex: 'status', width: 90, render: (v: number) => <Tag color={v === 1 ? 'green' : 'default'}>{v === 1 ? '营业中' : '停用'}</Tag> },
    { title: '更新时间', dataIndex: 'updated_at', width: 150, render: (v: number) => (v ? dayjs.unix(v).format('YYYY-MM-DD HH:mm') : '-') },
    {
      title: '操作', width: 150,
      render: (_, row) => (
        <Space size={4}>
          <Button size="small" onClick={() => openEdit(row)}>编辑</Button>
          <Popconfirm title="删除该门店？（有设备的门店不可删除）" onConfirm={() => remove(row)}>
            <Button size="small" danger>删除</Button>
          </Popconfirm>
        </Space>
      ),
    },
  ]

  return (
    <Card
      title="门店管理"
      extra={<Button type="primary" onClick={openCreate}>新增门店</Button>}
    >
      <Table rowKey="id" loading={loading} dataSource={data} columns={columns} pagination={false} />

      <Modal title={editing ? `编辑门店 #${editing.id}` : '新增门店'} open={open} onOk={submit} onCancel={() => setOpen(false)} destroyOnClose>
        <Form form={form} layout="vertical" initialValues={{ status: 1 }}>
          <Form.Item name="name" label="名称" rules={[{ required: true, message: '请输入门店名称' }]}>
            <Input placeholder="例如：望京店" maxLength={64} />
          </Form.Item>
          <Form.Item name="address" label="地址">
            <Input placeholder="（可选）" maxLength={255} />
          </Form.Item>
          <Form.Item name="status" label="状态">
            <Select options={[{ value: 1, label: '营业中' }, { value: 0, label: '停用' }]} />
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  )
}