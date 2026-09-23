import { useEffect, useMemo, useState } from 'react'
import { Alert, Button, Card, Form, Input, Modal, Select, Space, Table, Tag, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import { SettingOutlined, EditOutlined } from '@ant-design/icons'
import dayjs from 'dayjs'
import { fetchDeviceConfig, fetchDeviceTree, fetchStores, pushDeviceConfig, updateDevice } from '../api'
import { DEVICE_TYPE_TEXT, type Device } from '../api/types'
import { canWrite } from '../utils/role'

// 树形行：保留 children，antd Table 自动展开
interface Row extends Device {
  key: number
  children?: Row[]
}

function toRows(items: Device[]): Row[] {
  return items.map((d) => ({
    ...d,
    key: d.id,
    children: d.children?.length ? toRows(d.children) : undefined,
  }))
}

// 过滤后尽量保持父子关系：自身不匹配但子节点匹配时，子节点上提升为根
function filterRows(items: Row[], pred: (d: Row) => boolean): Row[] {
  const out: Row[] = []
  for (const d of items) {
    const children = d.children?.length ? filterRows(d.children, pred) : []
    if (pred(d)) {
      out.push({ ...d, children: children.length ? children : undefined })
    } else if (children.length) {
      out.push({ ...d, children })
    }
  }
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
      .then((items) => {
        const r = toRows(items)
        setAllRows(r)
        setRows(r)
      })
      .catch((e) => setError(String(e?.message ?? e)))
      .finally(() => setLoading(false))
    fetchStores()
      .then((r) => setStoreNames(new Map(r.items.map((i) => [i.id, i.name]))))
      .catch(() => setStoreNames(new Map()))
  }

  useEffect(() => {
    load()
  }, [])

  // 客户端筛选（保持树结构）
  useEffect(() => {
    let r = allRows
    if (typeFilter !== 'all') r = filterRows(r, (d) => d.device_type === Number(typeFilter))
    if (onlineFilter !== 'all') r = filterRows(r, (d) => d.status === Number(onlineFilter))
    setRows(r)
  }, [allRows, typeFilter, onlineFilter])

  const deviceOptions = useMemo(
    () => {
      const out: { id: number; key: string; device_type: number }[] = []
      const walk = (items: Row[], depth: number) => {
        for (const d of items) {
          out.push({ id: d.id, key: `${'　'.repeat(depth)}${d.name} (#${d.id})`, device_type: d.device_type })
          if (d.children?.length) walk(d.children, depth + 1)
        }
      }
      walk(allRows, 0)
      return out
    },
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
      title: '类型', dataIndex: 'device_type', width: 130,
      render: (v: number) => <Tag color={v <= 2 ? 'blue' : 'default'}>{DEVICE_TYPE_TEXT[v as keyof typeof DEVICE_TYPE_TEXT] ?? '设备'}</Tag>,
    },
    { title: '名称', dataIndex: 'name' },
    { title: '设备Key', dataIndex: 'device_key', width: 140, render: (v?: string) => v || '-' },
    {
      title: '门店', dataIndex: 'store_id', width: 120,
      render: (v: number) => (storeNames.has(v) ? storeNames.get(v) : `#${v}`),
    },
    {
      title: '状态', dataIndex: 'status', width: 90,
      render: (v: number) => <Tag color={v === 1 ? 'green' : 'red'}>{v === 1 ? '在线' : '离线'}</Tag>,
    },
    {
      title: '最后在线', dataIndex: 'last_seen_at', width: 160,
      render: (v: number) => (v > 0 ? dayjs.unix(v).format('YYYY-MM-DD HH:mm:ss') : '-'),
    },
    { title: 'CPU%', dataIndex: 'cpu', width: 70, render: (v?: number) => (v ? v.toFixed(0) : '-') },
    { title: '内存%', dataIndex: 'mem', width: 70, render: (v?: number) => (v ? v.toFixed(0) : '-') },
    { title: '磁盘%', dataIndex: 'disk', width: 70, render: (v?: number) => (v ? v.toFixed(0) : '-') },
    { title: 'FPS', dataIndex: 'fps', width: 60, render: (v?: number) => (v ? v.toFixed(0) : '-') },
    {
      title: '操作', width: 160, fixed: 'right',
      render: (_, r) => (
        <Space size={4}>
          {canWrite() && <Button size="small" icon={<EditOutlined />} onClick={() => {
            renameForm.setFieldsValue({ device_id: r.id, name: r.name })
            setRenameOpen(true)
          }}>改名</Button>}
          {canWrite() && r.device_type <= 2 && <Button size="small" icon={<SettingOutlined />} onClick={() => {
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
      title="设备管理（门店 → 主设备 → 子设备）"
      extra={
        <Space wrap>
          <Select
            style={{ width: 140 }} value={typeFilter} onChange={setTypeFilter} options={DEVICE_FILTERS}
          />
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
        scroll={{ x: 1200 }}
        expandable={{ defaultExpandAllRows: true }}
        pagination={false}
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
              options={deviceOptions.map((o) => ({ value: o.id, label: o.key }))}
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
            <Select showSearch placeholder="选择要改名的设备（含子设备）" options={deviceOptions.map((o) => ({ value: o.id, label: o.key }))} />
          </Form.Item>
          <Form.Item name="name" label="新名称" rules={[{ required: true, message: '请输入新名称' }]}>
            <Input placeholder="例如：东门边缘盒" maxLength={64} />
          </Form.Item>
        </Form>
      </Modal>
    </Card>
  )
}