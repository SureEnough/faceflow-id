import { useEffect, useState } from 'react'
import { Button, Card, Form, Input, Modal, Popconfirm, Radio, Select, Space, Table, Tag, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import dayjs from 'dayjs'
import { client, getToken, errMsg } from '../api/client'
import type { ApiResp } from '../api/client'

interface UserRow {
  id: number
  username: string
  role: string
  status: number
  created_at: number
}

const ROLE_TEXT: Record<string, string> = { admin: '管理员', operator: '操作员', viewer: '只读' }
const ROLE_COLOR: Record<string, string> = { admin: 'red', operator: 'blue', viewer: 'default' }

export default function Users() {
  const [rows, setRows] = useState<UserRow[]>([])
  const [loading, setLoading] = useState(false)
  const [open, setOpen] = useState(false)
  const [editing, setEditing] = useState<UserRow | null>(null)
  const [username, setUsername] = useState('')
  const [roleFilter, setRoleFilter] = useState('')
  const [statusFilter, setStatusFilter] = useState('')
  const [form] = Form.useForm()

  const roleMap: Record<string, number> = { admin: 0, operator: 1, viewer: 2 }

  const load = () => {
    setLoading(true)
    const params: Record<string, unknown> = {}
    if (username) params.username = username
    if (roleFilter !== '') params.role = roleMap[roleFilter]
    if (statusFilter !== '') params.status = statusFilter
    client
      .get<ApiResp<{ items: UserRow[] }>>('/users', { params, headers: { Authorization: `Bearer ${getToken()}` } })
      .then((r) => setRows(r.data.data?.items ?? []))
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }

  useEffect(load, [username, roleFilter, statusFilter])

  const openCreate = () => { setEditing(null); form.resetFields(); setOpen(true) }
  const openEdit = (u: UserRow) => { setEditing(u); form.setFieldsValue({ username: u.username, role: u.role, status: u.status }); setOpen(true) }

  const submit = async (values: { username: string; password?: string; role: string; status: number }) => {
    const headers = { Authorization: `Bearer ${getToken()}`, 'Content-Type': 'application/json' }
    try {
      if (editing) {
        await client.put(`/users/${editing.id}`, { role: roleMap[values.role], status: values.status, password: values.password || undefined }, { headers })
        message.success('账号已更新')
      } else {
        await client.post('/users', { username: values.username, password: values.password, role: roleMap[values.role], status: 1 }, { headers })
        message.success('账号已创建')
      }
      setOpen(false); load()
    } catch (e) {
      message.error(errMsg(e))
    }
  }

  const remove = async (id: number) => {
    try {
      await client.delete(`/users/${id}`, { headers: { Authorization: `Bearer ${getToken()}` } })
      message.success('账号已删除'); load()
    } catch (e) {
      message.error(errMsg(e))
    }
  }

  const columns: ColumnsType<UserRow> = [
    { title: 'ID', dataIndex: 'id', width: 70 },
    { title: '用户名', dataIndex: 'username' },
    {
      title: '角色', dataIndex: 'role', width: 100,
      render: (v: string) => <Tag color={ROLE_COLOR[v]}>{ROLE_TEXT[v] ?? v}</Tag>,
    },
    {
      title: '状态', dataIndex: 'status', width: 90,
      render: (v: number) => (v === 1 ? <Tag color="green">启用</Tag> : <Tag color="red">停用</Tag>),
    },
    { title: '创建时间', dataIndex: 'created_at', render: (v: number) => (v ? dayjs.unix(v).format('YYYY-MM-DD HH:mm') : '-') },
    {
      title: '操作', width: 140,
      render: (_, u) => (
        <Space size={4}>
          <Button size="small" onClick={() => openEdit(u)}>编辑</Button>
          <Popconfirm title={`删除账号 ${u.username}？`} onConfirm={() => remove(u.id)}>
            <Button size="small" danger>删除</Button>
          </Popconfirm>
        </Space>
      ),
    },
  ]

  return (
    <Card
      title="账号管理（仅管理员）"
      extra={
        <Space wrap>
          <Input.Search
            style={{ width: 200 }} placeholder="按用户名筛选" allowClear
            onSearch={(v) => setUsername(v.trim())}
          />
          <Select
            style={{ width: 120 }} placeholder="角色" allowClear value={roleFilter}
            onChange={(v) => setRoleFilter(v ?? '')}
            options={[
              { value: 'admin', label: '管理员' },
              { value: 'operator', label: '操作员' },
              { value: 'viewer', label: '只读' },
            ]}
          />
          <Select
            style={{ width: 110 }} placeholder="状态" allowClear value={statusFilter}
            onChange={(v) => setStatusFilter(v ?? '')}
            options={[{ value: '1', label: '启用' }, { value: '0', label: '停用' }]}
          />
          <Button type="primary" onClick={openCreate}>新增账号</Button>
        </Space>
      }
    >
      <Table
        rowKey="id" loading={loading} dataSource={rows} columns={columns} pagination={false}
        rowClassName={(r) => (r.status === 0 ? 'ant-table-row-disabled' : '')}
        style={{ minHeight: 200 }}
      />

      <Modal
        title={editing ? `编辑账号 ${editing.username}` : '新增账号'} open={open}
        onOk={() => form.submit()} onCancel={() => setOpen(false)} destroyOnClose
      >
        <Form form={form} layout="vertical" onFinish={submit}>
          <Form.Item name="username" label="用户名" rules={[{ required: !editing, message: '请输入用户名' }]}>
            <Input disabled={!!editing} placeholder="username" />
          </Form.Item>
          <Form.Item name="password" label={editing ? '新密码（留空不改）' : '密码'} rules={[{ required: !editing && true, min: 6, message: '至少 6 位' }]}>
            <Input.Password placeholder={editing ? '留空则不修改' : '至少 6 位'} />
          </Form.Item>
          <Form.Item name="role" label="角色" initialValue="viewer" rules={[{ required: true }]}>
            <Radio.Group
              options={[
                { value: 'admin', label: '管理员' },
                { value: 'operator', label: '操作员' },
                { value: 'viewer', label: '只读' },
              ]}
            />
          </Form.Item>
          <Form.Item name="status" label="状态" initialValue={1} rules={[{ required: true }]}>
            <Radio.Group options={[{ value: 1, label: '启用' }, { value: 0, label: '停用' }]} />
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  )
}