import { useEffect, useState } from 'react'
import {
  Button, Card, Dropdown, Form, Input, Modal, Popconfirm, Radio, Select, Space, Table, Tag, Typography, Upload, message,
} from 'antd'
import type { ColumnsType } from 'antd/es/table'
import { DownOutlined, InboxOutlined, UploadOutlined } from '@ant-design/icons'
import dayjs from 'dayjs'
import {
  appendCustomerFeature, createCustomer, deleteCustomer, downloadImportTemplate,
  exportCustomersXLSX, fetchCustomers, fetchStores, fetchVisitStats, importCustomers, updateCustomer,
} from '../api'
import { downloadCsv, errMsg } from '../api/client'
import { canWrite, isAdmin } from '../utils/role'
import AvatarUploadWithCrop from '../components/AvatarUploadWithCrop'
import type { Customer, ImportResult, VisitStats } from '../api/types'

const STATUS_TEXT: Record<number, string> = { 0: '正常', 1: '黑名单', 2: '注销', 3: '离职' }
const STATUS_COLOR: Record<number, string> = { 0: 'green', 1: 'red', 2: 'default', 3: 'orange' }
const GENDER_TEXT: Record<number, string> = { 0: '未知', 1: '男', 2: '女' }

interface Row extends Customer { key: number }

// 照片展示：有 URL 用 URL（对象存储），否则视为 base64 文本直接显示
function photoImg(path?: string, url?: string, alt = '照片') {
  const src = url || (path ? `data:image/jpeg;base64,${path}` : '')
  if (!src) return null
  return (
    <img
      src={src} alt={alt}
      style={{ maxWidth: 140, maxHeight: 140, borderRadius: 8, objectFit: 'cover', border: '1px solid #eee' }}
    />
  )
}



export default function Customers() {
  const [data, setData] = useState<Row[]>([])
  const [total, setTotal] = useState(0)
  const [page, setPage] = useState(1)
  const [personType, setPersonType] = useState<number | undefined>()
  const [status, setStatus] = useState<number | undefined>()
  const [name, setName] = useState('')
  const [storeFilter, setStoreFilter] = useState<number | undefined>()
  const [loading, setLoading] = useState(false)

  // 新增
  const [createOpen, setCreateOpen] = useState(false)
  const [createForm] = Form.useForm()
  const [avatarB64, setAvatarB64] = useState('')
  // 编辑 / 追加特征：头像（裁剪后自动提取特征）
  const [editAvatarB64, setEditAvatarB64] = useState('')
  const [featAvatarB64, setFeatAvatarB64] = useState('')
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
  // 门店选项（新增/编辑表单）
  const [stores, setStores] = useState<{ id: number; name: string }[]>([])
  const storeName = (id?: number) => (id ? stores.find((s) => s.id === id)?.name ?? `#${id}` : '-')
  // 模板导入
  const [importOpen, setImportOpen] = useState(false)
  const [importing, setImporting] = useState(false)
  const [importResult, setImportResult] = useState<ImportResult | null>(null)

  const load = () => {
    setLoading(true)
    fetchCustomers({
      page, page_size: 20, person_type: personType, status, store_id: storeFilter,
      name: name || undefined,
    })
      .then((r) => { setTotal(r.total); setData(r.items.map((i) => ({ ...i, key: i.id }))) })
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }

  useEffect(load, [page, personType, status, storeFilter, name])

  useEffect(() => {
    fetchStores({ status: 1 }).then((r) => setStores(r.items.map((s) => ({ id: s.id, name: s.name })))).catch(() => {})
  }, [])

 const submitCreate = async () => {
    const values = await createForm.validateFields()
    const body: Record<string, unknown> = { ...values }
    if (avatarB64) body.id_photo = avatarB64 // 上传了头像：后端自动提取特征
    try {
      const r = await createCustomer(body)
      if (r.history) {
        message.success(`录入成功，历史来访 ${r.history.total_visits} 次 / ${r.history.visit_days} 天`)
      } else {
        message.success('录入成功')
      }
      setCreateOpen(false); createForm.resetFields(); setAvatarB64(''); load()
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
      store_id: row.store_id ?? undefined,
      face_feature: '',
    })
    setEditAvatarB64('')
    setEditOpen(true)
  }

  const submitEdit = async () => {
    if (!editing) return
    const values = await editForm.validateFields()
    const body: Record<string, unknown> = { ...values }
    if (!body.face_feature) delete body.face_feature // 空特征不替换
    if (editAvatarB64) body.id_photo = editAvatarB64 // 上传了头像：自动替换特征
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
    const body: Record<string, unknown> = {}
    if (face_feature) body.face_feature = face_feature
    if (featAvatarB64) body.id_photo = featAvatarB64
    if (!body.face_feature && !body.id_photo) {
      message.warning('请上传头像或粘贴人脸特征')
      return
    }
    try {
      await appendCustomerFeature(featTarget.id, body)
      message.success('特征已追加')
      setFeatOpen(false); featForm.resetFields(); setFeatAvatarB64(''); load()
    } catch (e) {
      message.error(errMsg(e))
    }
  }

  // 模板导入：选择 .xlsx 后直接上传
  const doImport = async (file: File) => {
    setImporting(true); setImportResult(null)
    try {
      const r = await importCustomers(file)
      setImportResult(r)
    } catch (e) {
      message.error(errMsg(e))
    } finally {
      setImporting(false)
    }
    return false // 阻止 antd 默认上传
  }

  const columns: ColumnsType<Row> = [
    { title: 'ID', dataIndex: 'id', width: 70 },
    {
      title: '类型', dataIndex: 'person_type', width: 90,
      render: (v: number) => (v === 1 ? <Tag color="geekblue">内部人员</Tag> : <Tag color="green">顾客</Tag>),
    },
    { title: '姓名', dataIndex: 'name' },
    { title: '门店', dataIndex: 'store_id', width: 110, render: (v?: number) => storeName(v) },
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
          {canWrite() && <Button size="small" onClick={() => openEdit(row)}>编辑</Button>}
          {canWrite() && <Button size="small" onClick={() => { setFeatTarget(row); setFeatOpen(true) }}>特征</Button>}
          {isAdmin() && (
            <Popconfirm
              title={row.person_type === 1 ? '标记该员工离职？' : '注销该顾客档案？'}
              onConfirm={() => remove(row)}
            >
              <Button size="small" danger>{row.person_type === 1 ? '离职' : '注销'}</Button>
            </Popconfirm>
          )}
        </Space>
      ),
    },
  ]

  return (
    <Card
      title={`人员管理（${total} 条）`}
      extra={
        <Space wrap>
          <Input.Search
            style={{ width: 180 }} placeholder="按姓名筛选" allowClear
            onSearch={(v) => { setPage(1); setName(v.trim()) }}
          />
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
          <Select
            style={{ width: 160 }} placeholder="按门店筛选" allowClear showSearch optionFilterProp="label"
            value={storeFilter}
            onChange={(v) => { setPage(1); setStoreFilter(v) }}
            options={stores.map((s) => ({ value: s.id, label: s.name }))}
          />
          <Dropdown
            menu={{
              items: [
                { key: 'csv', label: '导出 CSV' },
                { key: 'xlsx', label: '导出 Excel (.xlsx)' },
              ],
              onClick: ({ key }) => {
                const params = { person_type: personType, status, store_id: storeFilter }
                if (key === 'csv') {
                  downloadCsv('/export/customers.csv', params, 'customers.csv').catch((e) => message.error(errMsg(e)))
                } else {
                  exportCustomersXLSX(params).catch((e) => message.error(errMsg(e)))
                }
              },
            }}
          >
            <Button>导出 <DownOutlined /></Button>
          </Dropdown>
          {canWrite() && <Button onClick={() => { setImportResult(null); setImportOpen(true) }}>模板导入</Button>}
          {canWrite() && <Button type="primary" onClick={() => setCreateOpen(true)}>新增人员</Button>}
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
          <AvatarUploadWithCrop
            value={avatarB64}
            onChange={setAvatarB64}
            extra="上传后先裁剪，再自动调用人脸识别服务提取特征；也可在下方手动粘贴特征"
          />
          <Form.Item name="store_id" label="归属门店（可选）">
            <Select
              allowClear placeholder="选择门店"
              options={stores.map((s) => ({ value: s.id, label: s.name }))}
            />
          </Form.Item>
          <Form.Item name="face_feature" label="人脸特征 (base64, 可选)" extra="有头像时可不填，由系统自动提取">
            <Input.TextArea rows={2} placeholder="base64 编码的 512 维特征（选填）" />
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
          <Form.Item name="store_id" label="归属门店（可选）">
            <Select
              allowClear placeholder="选择门店"
              options={stores.map((s) => ({ value: s.id, label: s.name }))}
            />
          </Form.Item>
          <AvatarUploadWithCrop
            value={editAvatarB64}
            onChange={setEditAvatarB64}
            label="替换头像（正脸照片）"
            extra="上传裁剪后自动提取特征替换主特征；也可在下方手动粘贴特征"
          />
          <Form.Item name="face_feature" label="替换人脸特征（留空不替换）">
            <Input.TextArea rows={2} placeholder="base64 编码的 512 维特征（或 上传头像后由系统提取）" />
          </Form.Item>
        </Form>
      </Modal>

      {/* 追加特征 */}
      <Modal title={`追加特征 #${featTarget?.id ?? ''}`} open={featOpen} onOk={submitFeature} onCancel={() => setFeatOpen(false)} destroyOnClose>
        <Form form={featForm} layout="vertical">
          <AvatarUploadWithCrop
            value={featAvatarB64}
            onChange={setFeatAvatarB64}
            label="上传头像（推荐）"
            extra="上传裁剪后自动提取特征追加；或粘贴特征"
          />
          <Form.Item name="face_feature" label="人脸特征 (base64)">
            <Input.TextArea rows={3} placeholder="base64 编码的 512 维特征（也可上传头像）" />
          </Form.Item>
        </Form>
      </Modal>

      {/* 模板导入 */}
      <Modal
        title="批量导入人员（Excel）" open={importOpen} footer={null}
        onCancel={() => setImportOpen(false)} destroyOnClose width={720}
      >
        <Space direction="vertical" style={{ width: '100%' }}>
          <Button icon={<UploadOutlined />} onClick={() => downloadImportTemplate().catch((e) => message.error(errMsg(e)))}>
            下载导入模板（含填写说明与示例）
          </Button>
          <Upload.Dragger
            accept=".xlsx"
            multiple={false}
            showUploadList={false}
            beforeUpload={(file) => doImport(file)}
          >
            <p className="ant-upload-drag-icon"><InboxOutlined /></p>
            <p className="ant-upload-text">点击或拖拽 .xlsx 文件到此处上传</p>
            <p className="ant-upload-hint">表头固定：人员类型/姓名/身份证号/工号/部门/性别/出生日期/住址/头像；头像在 Excel 内嵌入图片，后台自动人脸识别提取特征</p>
          </Upload.Dragger>

          {importing && <Typography.Text type="secondary">正在导入并提取人脸特征…（每张头像调用一次 face-service）</Typography.Text>}

          {importResult && (
            <Space direction="vertical" style={{ width: '100%' }}>
              <Typography.Text>
                共 <b>{importResult.total}</b> 行，成功 <b style={{ color: '#389e0d' }}>{importResult.success}</b> 行
                {importResult.failed.length > 0 && <>，失败 <b style={{ color: '#cf1322' }}>{importResult.failed.length}</b> 行</>}
              </Typography.Text>
              {importResult.failed.length > 0 && (
                <Table
                  size="small" rowKey="row" pagination={false} dataSource={importResult.failed}
                  columns={[
                    { title: '行号', dataIndex: 'row', width: 70 },
                    { title: '姓名', dataIndex: 'name', width: 140 },
                    { title: '失败原因', dataIndex: 'reason' },
                  ]}
                  scroll={{ y: 240 }}
                />
              )}
            </Space>
          )}
        </Space>
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
              { k: 'store', label: '归属门店', value: storeName(detail.store_id) },
              { k: 'photo', label: '证件照', value: photoImg(detail.id_photo_path, detail.id_photo_url, '证件照') },
              { k: 'live', label: '活体照', value: photoImg(detail.live_photo_path, detail.live_photo_url, '活体照') },
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