import { useEffect, useMemo, useState } from 'react'
import { Alert, Button, Card, Form, Input, Modal, Select, Space, Table, Tag, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import { SettingOutlined, EditOutlined } from '@ant-design/icons'
import dayjs from 'dayjs'
import { fetchDeviceConfig, fetchDeviceTree, fetchStores, pushDeviceConfig, updateDevice } from '../api'
import { DEVICE_TYPE_TEXT, type Device } from '../api/types'
import { canWrite } from '../utils/role'

interface Row extends Device {
  key: number
  parent_name: string // 绑定的父设备名（主设备为空）
  is_primary: boolean
}

// 树展平为扁平行（携带父设备名）
function flattenTree(items: Device[]): Row[] {
  const out: Row[] = []
  const walk = (list: Device[], parent?: Device) => {
    for (const d of list) {
      out.push({
        ...d,
        key: d.id,
        parent_name: parent ? `${parent.name} (#${parent.id})` : '',
        is_primary: parent === undefined,
      })
      if (d.children?.length) walk(d.children, d)
    }
  }
  walk(items)
  return out
}

const DEVICE_FILTERS = [
  { value: 'all', label: '全部类型' },
  ...Object.entries(DEVICE_TYPE_TEXT).map(([v, t]) => ({ value: v, label: t })),
]

export default function Devices() {
  const [allRows, setAllRows] = useState<Row[]>([])
  const [rows, setRows] = useState<Row[]>([])
  const [storeNames, setStoreNames] = useState<Map<number, string>>(new Map())
  const [typeFilter, setTypeFilter] = useState('all')
  const [onlineFilter, setOnlineFilter] = useState('all')
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')

  // 配置下发弹窗
  const [pushOpen, setPushOpen] = useState(false)
  const [pushLoading, setPushLoading] = useState(false)
  const [pushForm] = Form.useForm<{ device_id: number; config: string }>()
  // 编辑名称弹窗
  const [renameOpen, setRenameOpen] = useState(false)
  const [renameLoading, setRenameLoading] = useState(false)
  const [renameForm] = Form.useForm<{ device_id: number; name: string }>()

  const load = () => {
    setLoading(true); setError('')
    fetchDeviceTree()
      .then((items) => setAllRows(flattenTree(items)))
      .catch((e) => setError(String(e?.message ?? e)))
      .finally(() => setLoading(false))
    fetchStores()
      .then((r) => setStoreNames(new Map(r.items.map((i) => [i.id, i.name]))))
      .catch(() => setStoreNames(new Map()))
  }

  useEffect(() => {
    load()
  }, [])

  useEffect(() => {
    let r = allRows
    if (typeFilter !== 'all') r = r.filter((d) => d.device_type === Number(typeFilter))
    if (onlineFilter !== 'all') r = r.filter((d) => d.status === Number(onlineFilter))
    setRows(r)
  }, [allRows, typeFilter, onlineFilter])

  const deviceOptions = useMemo(
    () => allRows.map((d) => ({ value: d.id, label: `${d.name} (#${d.id})` })),
    [allRows],
  )

  const onSelectDevice = async (id: number) => {
    setPushLoading(true)
    try {
      const { config } = await fetchDeviceConfig(id)
      pushForm.setFieldsValue({ device_id: id, config: JSON.stringify(config ?? {}, null, 2) })
    } catch (e) {
      message.error(String((e as Error).message ?? e))
    } finally {
      setPushLoading(false)
    }
  }

  const onPush = async () => {
    const values = await pushForm.validateFields()
    let parsed: Record<string, unknown>
    try {
      parsed = JSON.parse(values.config)
    } catch {
      message.error('配置不是合法 JSON')
      return
    }
    setPushLoading(true)
    try {
      await pushDeviceConfig(values.device_id, parsed)
      message.success('已下发，设备将在轮询周期内热重载')
      setPushOpen(false)
    } catch (e) {
      message.error(String((e as Error).message ?? e))
    } finally {
      setPushLoading(false)
    }
  }

  const onRename = async () => {
    const values = await renameForm.validateFields()
    setRenameLoading(true)
    try {
      await updateDevice(values.device_id, { name: values.name })
      message.success('已改名')
      setRenameOpen(false)
      renameForm.resetFields()
      load()
    } catch (e) {
      message.error(String((e as Error).message ?? e))
    } finally {
      setRenameLoading(false)
    }
  }

  const columns: ColumnsType<Row> = [
    {
      title: '类型', dataIndex: 'device_type', width: 110,
      render: (v: number) => <Tag color={v <= 2 ? 'blue' : 'default'}>{DEVICE_TYPE_TEXT[v as keyof typeof DEVICE_TYPE_TEXT] ?? '设备'}</Tag>,
    },
    { title: '名称', dataIndex: 'name', width: 160 },
    { title: '设备Key', dataIndex: 'device_key', width: 130, render: (v?: string) => v || '-' },
    {
      title: '绑定设备', dataIndex: 'parent_name', width: 180,
      render: (v: string, r) => (r.is_primary ? <Tag color="purple">主设备</Tag> : <span>{v}</span>),
    },
    {
      title: '门店', dataIndex: 'store_id', width: 110,
      render: (v: number) => (storeNames.has(v) ? storeNames.get(v) : `#${v}`),
    },
    {
      title: '状态', dataIndex: 'status', width: 80,
      render: (v: number) => <Tag color={v === 1 ? 'green' : 'red'}>{v === 1 ? '在线' : '离线'}</Tag>,
    },
    {
      title: '最后在线', dataIndex: 'last_seen_at', width: 150,
      render: (v: number) => (v > 0 ? dayjs.unix(v).format('YYYY-MM-DD HH:mm:ss') : '-'),
    },
    {
      title: '操作', width: 150, fixed: 'right',
      render: (_, r) => (
        <Space size={4}>
          {canWrite() && <Button size="small" icon={<EditOutlined />} onClick={() => {
            renameForm.setFieldsValue({ device_id: r.id, name: r.name })
            setRenameOpen(true)
          }}>改名</Button>}
          {canWrite() && r.is_primary && <Button size="small" icon={<SettingOutlined />} onClick={() => {
            pushForm.setFieldsValue({ device_id: r.id })
            setPushOpen(true)
            onSelectDevice(r.id)
          }}>配置</Button>}
        </Space>
      ),
    },
  ]

  return (
    <Card
      title={`设备管理（${rows.length} 台）`}
      extra={
        <Space wrap>
          <Select style={{ width: 140 }} value={typeFilter} onChange={setTypeFilter} options={DEVICE_FILTERS} />
          <Select
            style={{ width: 110 }} value={onlineFilter} onChange={setOnlineFilter}
            options={[
              { value: 'all', label: '全部状态' },
              { value: '1', label: '在线' },
              { value: '0', label: '离线' },
            ]}
          />
        </Space>
      }
    >
      {error && <Alert type="error" showIcon message={error} style={{ marginBottom: 12 }} />}
      <Table
        rowKey="id" loading={loading} dataSource={rows} columns={columns} size="small"
        scroll={{ x: 950 }}
        pagination={{ pageSize: 50, showTotal: (t) => `共 ${t} 条` }}
      />

      <Modal
        title="向主设备下发配置（远程配置优先，门店边缘盒轮询后自动应用）"
        open={pushOpen}
        onCancel={() => setPushOpen(false)}
        onOk={onPush}
        confirmLoading={pushLoading}
        width={720}
        okText="下发"
        cancelText="取消"
      >
        <Form form={pushForm} layout="vertical">
          <Form.Item name="device_id" label="主设备" rules={[{ required: true, message: '请选择设备' }]}>
            <Select
              showSearch
              placeholder="选择边缘盒 / 录入电脑端"
              options={deviceOptions.filter((o) => {
                const d = allRows.find((r) => r.id === o.value)
                return d && d.is_primary
              })}
              onSelect={onSelectDevice}
              loading={pushLoading}
            />
          </Form.Item>
          <Form.Item
            name="config"
            label="配置 JSON（cameras / 阈值 / report_endpoint 等；设备身份与 Web 安全字段以本地为准）"
            rules={[{ required: true, message: '请输入配置 JSON' }]}
          >
            <Input.TextArea rows={12} style={{ fontFamily: 'monospace' }} placeholder='{\n  "det_thresh": 0.5,\n  "cameras": [...]\n}' />
          </Form.Item>
        </Form>
      </Modal>

      <Modal
        title="编辑设备名称"
        open={renameOpen}
        onCancel={() => setRenameOpen(false)}
        onOk={onRename}
        confirmLoading={renameLoading}
        okText="保存"
        cancelText="取消"
      >
        <Form form={renameForm} layout="vertical">
          <Form.Item name="device_id" label="设备" rules={[{ required: true, message: '请选择设备' }]}>
            <Select showSearch placeholder="选择要改名的设备（含子设备）" options={deviceOptions} />
          </Form.Item>
          <Form.Item name="name" label="新名称" rules={[{ required: true, message: '请输入新名称' }]}>
            <Input placeholder="例如：东门边缘盒" maxLength={64} />
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  )
}