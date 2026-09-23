import { useEffect, useState } from 'react'
import {
  Button, Card, Form, Input, Modal, Popconfirm, Radio, Select, Space, Table, Tag, Typography, message,
} from 'antd'
import type { ColumnsType } from 'antd/es/table'
import dayjs from 'dayjs'
import {
  appendCustomerFeature, createCustomer, deleteCustomer, fetchCustomers, fetchVisitStats, updateCustomer,
} from '../api'
import { downloadCsv, errMsg } from '../api/client'
import type { Customer, VisitStats } from '../api/types'

const STATUS_TEXT: Record<number, string> = { 0: '正常', 1: '黑名单', 2: '注销', 3: '离职' }
const STATUS_COLOR: Record<number, string> = { 0: 'green', 1: 'red', 2: 'default', 3: 'orange' }
const GENDER_TEXT: Record<number, string> = { 0: '未知', 1: '男', 2: '女' }

interface Row extends Customer { key: number }

export default function Customers() {
  const [data, setData] = useState<Row[]>([])
  const [total, setTotal] = useState(0)
  const [page, setPage] = useState(1)
  const [personType, setPersonType] = useState<number | undefined>()
  const [status, setStatus] = useState<number | undefined>()
  const [loading, setLoading] = useState(false)

  // 新增
  const [createOpen, setCreateOpen] = useState(false)
  const [createForm] = Form.useForm()
  // 编辑
  const [editOpen, setEditOpen] = useState(false)
  const [editForm] = Form.useForm()
  const [editing, setEditing] = useState<Row | null>(null)
  // 追加特征
  const [featOpen, setFeatOpen] = useState(false)
  const [featForm] = Form.useForm()
  const [featTarget, setFeatTarget] = useState<Row | null>(null)
  // 详情（历史来访）
  const [detailOpen, setDetailOpen] = useState(false)
  const [detail, setDetail] = useState<Row | null>(null)
  const [visits, setVisits] = useState<VisitStats | null>(null)

  const load = () => {
    setLoading(true)
    fetchCustomers({ page, page_size: 20, person_type: personType, status })
      .then((r) => { setTotal(r.total); setData(r.items.map((i) => ({ ...i, key: i.id }))) })
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }

  useEffect(load, [page, personType, status])

  const submitCreate = async () => {
    const values = await createForm.validateFields()
    try {
      const r = await createCustomer(values)
      if (r.history) {
        message.success(`录入成功，历史来访 ${r.history.total_visits} 次 / ${r.history.visit_days} 天`)
      } else {
        message.success('录入成功')
      }
      setCreateOpen(false); createForm.resetFields(); load()
    } catch (e) {
      message.error(errMsg(e))
    }
  }

  const openEdit = (row: Row) => {
    setEditing(row)
    editForm.setFieldsValue({
      name: row.name,
      id_card_no: row.id_card_no,
      birth_date: row.birth_date ? dayjs.unix(row.birth_date).format('YYYY-MM-DD') : undefined,
      gender: row.gender,
      address: row.address,
      staff_no: row.staff_no,
      department: row.department,
      status: row.status,
      face_feature: '',
    })
    setEditOpen(true)
  }

  const submitEdit = async () => {
    if (!editing) return
    const values = await editForm.validateFields()
    const body: Record<string, unknown> = { ...values }
    if (!body.face_feature) delete body.face_feature // 空特征不替换
    try {
      await updateCustomer(editing.id, body)
      message.success('已保存')
      setEditOpen(false); load()
    } catch (e) {
      message.error(errMsg(e))
    }
  }

  const remove = async (row: Row) => {
    try {
      await deleteCustomer(row.id, row.person_type === 1 ? { staff: true } : undefined)
      message.success(row.person_type === 1 ? '已标记离职' : '已注销')
      load()
    } catch (e) {
      message.error(errMsg(e))
    }
  }

  const openDetail = async (row: Row) => {
    setDetail(row); setDetailOpen(true); setVisits(null)
    if (row.person_type === 0) {
      try {
        setVisits(await fetchVisitStats(row.id))
      } catch (e) {
        message.error(errMsg(e))
      }
    }
  }

  const submitFeature = async () => {
    const { face_feature } = await featForm.validateFields()
    if (!featTarget) return
    try {
      await appendCustomerFeature(featTarget.id, face_feature)
      message.success('特征已追加')
      setFeatOpen(false); featForm.resetFields(); load()
    } catch (e) {
      message.error(errMsg(e))
    }
  }

  const columns: ColumnsType<Row> = [
    { title: 'ID', dataIndex: 'id', width: 70 },
    {
      title: '类型', dataIndex: 'person_type', width: 90,
      render: (v: number) => (v === 1 ? <Tag color="geekblue">内部人员</Tag> : <Tag color="green">顾客</Tag>),
    },
    { title: '姓名', dataIndex: 'name' },
    { title: '身份证号', dataIndex: 'id_card_no', ellipsis: true },
    { title: '工号', dataIndex: 'staff_no' },
    { title: '部门', dataIndex: 'department' },
    { title: '性别', dataIndex: 'gender', width: 60, render: (v?: number) => GENDER_TEXT[v ?? 0] },
    {
      title: '状态', dataIndex: 'status', width: 80,
      render: (v: number) => <Tag color={STATUS_COLOR[v]}>{STATUS_TEXT[v] ?? v}</Tag>,
    },
    {
      title: '更新时间', dataIndex: 'updated_at', width: 150,
      render: (v: number) => (v ? dayjs.unix(v).format('YYYY-MM-DD HH:mm') : '-'),
    },
    {
      title: '操作', width: 220, fixed: 'right',
      render: (_, row) => (
        <Space size={4}>
          <Button size="small" onClick={() => openDetail(row)}>详情</Button>
          <Button size="small" onClick={() => openEdit(row)}>编辑</Button>
          <Button size="small" onClick={() => { setFeatTarget(row); setFeatOpen(true) }}>特征</Button>
          <Popconfirm
            title={row.person_type === 1 ? '标记该员工离职？' : '注销该顾客档案？'}
            onConfirm={() => remove(row)}
          >
            <Button size="small" danger>{row.person_type === 1 ? '离职' : '注销'}</Button>
          </Popconfirm>
        </Space>
      ),
    },
  ]

  return (
    <Card
      title="人员库"
      extra={
        <Space>
          <Select
            style={{ width: 130 }} placeholder="人员类型" allowClear
            value={personType}
            onChange={(v) => { setPage(1); setPersonType(v) }}
            options={[{ value: 0, label: '顾客' }, { value: 1, label: '内部人员' }]}
          />
          <Select
            style={{ width: 120 }} placeholder="状态" allowClear
            value={status}
            onChange={(v) => { setPage(1); setStatus(v) }}
            options={[
              { value: 0, label: '正常' }, { value: 1, label: '黑名单' },
              { value: 2, label: '注销' }, { value: 3, label: '离职' },
            ]}
          />
          <Button
            onClick={() =>
              downloadCsv('/export/customers.csv', { person_type: personType, status }, 'customers.csv')
                .catch((e) => message.error(errMsg(e)))
            }
          >
            导出 CSV
          </Button>
          <Button type="primary" onClick={() => setCreateOpen(true)}>新增人员</Button>
        </Space>
      }
    >
      <Table
        rowKey="id" loading={loading} dataSource={data} columns={columns} scroll={{ x: 1100 }}
        pagination={{ current: page, pageSize: 20, total, onChange: setPage, showTotal: (t) => `共 ${t} 条` }}
      />

      {/* 新增 */}
      <Modal title="新增人员档案" open={createOpen} onOk={submitCreate} onCancel={() => setCreateOpen(false)} destroyOnClose>
        <Form form={createForm} layout="vertical" initialValues={{ person_type: 0, gender: 0 }}>
          <Form.Item name="person_type" label="类型" rules={[{ required: true }]}>
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
                  <Form.Item name="gender" label="性别">
                    <Radio.Group options={[{ value: 1, label: '男' }, { value: 2, label: '女' }, { value: 0, label: '未知' }]} />
                  </Form.Item>
                  <Form.Item name="address" label="住址">
                    <Input placeholder="（可选）" />
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

      {/* 编辑 */}
      <Modal title={`编辑档案 #${editing?.id ?? ''}`} open={editOpen} onOk={submitEdit} onCancel={() => setEditOpen(false)} destroyOnClose>
        <Form form={editForm} layout="vertical">
          <Form.Item name="name" label="姓名" rules={[{ required: true }]}>
            <Input />
          </Form.Item>
          {editing?.person_type === 0 ? (
            <>
              <Form.Item name="id_card_no" label="身份证号">
                <Input maxLength={18} />
              </Form.Item>
              <Form.Item name="birth_date" label="出生日期">
                <Input placeholder="1990-01-01" />
              </Form.Item>
              <Form.Item name="gender" label="性别">
                <Radio.Group options={[{ value: 1, label: '男' }, { value: 2, label: '女' }, { value: 0, label: '未知' }]} />
              </Form.Item>
              <Form.Item name="address" label="住址">
                <Input />
              </Form.Item>
            </>
          ) : (
            <>
              <Form.Item name="staff_no" label="工号">
                <Input />
              </Form.Item>
              <Form.Item name="department" label="部门">
                <Input />
              </Form.Item>
            </>
          )}
          <Form.Item name="status" label="状态">
            <Select options={[
              { value: 0, label: '正常' }, { value: 1, label: '黑名单' },
              { value: 2, label: '注销' }, { value: 3, label: '离职' },
            ]} />
          </Form.Item>
          <Form.Item name="face_feature" label="替换人脸特征（留空不替换）">
            <Input.TextArea rows={2} placeholder="base64 编码的 512 维特征" />
          </Form.Item>
        </Form>
      </Modal>

      {/* 追加特征 */}
      <Modal title={`追加特征 #${featTarget?.id ?? ''}`} open={featOpen} onOk={submitFeature} onCancel={() => setFeatOpen(false)} destroyOnClose>
        <Form form={featForm} layout="vertical">
          <Form.Item name="face_feature" label="人脸特征 (base64)" rules={[{ required: true, message: '请输入特征' }]}>
            <Input.TextArea rows={3} placeholder="base64 编码的 512 维特征" />
          </Form.Item>
        </Form>
      </Modal>

      {/* 详情 */}
      <Modal
        title={`人员详情：${detail?.name ?? ''}`} open={detailOpen} footer={null} onCancel={() => setDetailOpen(false)}
      >
        {detail && (
          <Table
            size="small" pagination={false} rowKey="k"
            dataSource={[
              { k: 'id', label: 'ID', value: detail.id },
              { k: 'type', label: '类型', value: detail.person_type === 1 ? '内部人员' : '顾客' },
              { k: 'idcard', label: '身份证号', value: detail.id_card_no ?? '-' },
              { k: 'staff', label: '工号', value: detail.staff_no ?? '-' },
              { k: 'dept', label: '部门', value: detail.department ?? '-' },
              { k: 'gender', label: '性别', value: GENDER_TEXT[detail.gender ?? 0] },
              { k: 'status', label: '状态', value: STATUS_TEXT[detail.status] ?? detail.status },
              { k: 'created', label: '建档时间', value: dayjs.unix(detail.created_at).format('YYYY-MM-DD HH:mm') },
            ]}
            columns={[
              { title: '字段', dataIndex: 'label', width: 100 },
              { title: '值', dataIndex: 'value' },
            ]}
          />
        )}
        {detail?.person_type === 0 && (
          <Card size="small" title="历史来访统计" style={{ marginTop: 12 }}>
            {visits ? (
              <Space size={24} wrap>
                <span>累计来访：<b style={{ color: '#cf1322' }}>{visits.total_visits}</b> 次</span>
                <span>覆盖天数：<b>{visits.visit_days}</b> 天</span>
                <span>首次到访：{visits.first_visit_at || '-'}</span>
                <span>最近到访：{visits.last_visit_at || '-'}</span>
              </Space>
            ) : (
              <Typography.Text type="secondary">加载中…</Typography.Text>
            )}
          </Card>
        )}
      </Modal>
    </Card>
  )
}