import { useEffect, useState } from 'react'
import { Button, Card, Popconfirm, Radio, Space, Table, Tag, message } from 'antd'
import type { ColumnsType } from 'antd/es/table'
import dayjs from 'dayjs'
import { client, getToken, errMsg } from '../api/client'
import type { ApiResp } from '../api/client'

interface TokenRow {
  jti: string
  subject_kind: 'user' | 'device'
  subject_id: number
  subject_name: string
  role: string
  issued_at: number
  expires_at: number
  revoked_at: number | null
  revoked_by: number
  last_used_at: number
}

const KIND_TEXT: Record<string, string> = { user: '后台用户', device: '设备' }
const KIND_COLOR: Record<string, string> = { user: 'blue', device: 'gold' }
const ROLE_TEXT: Record<string, string> = { admin: '管理员', operator: '操作员', viewer: '只读', device: '设备' }

function fmt(v: number | null | undefined): string {
  return v ? dayjs.unix(v).format('YYYY-MM-DD HH:mm') : '-'
}

export default function Tokens() {
  const role = localStorage.getItem('role')
  const isAdmin = role === 'admin'
  const [rows, setRows] = useState<TokenRow[]>([])
  const [loading, setLoading] = useState(false)
  const [kind, setKind] = useState('')
  const [status, setStatus] = useState('active')

  const load = () => {
    setLoading(true)
    const params = new URLSearchParams()
    if (kind) params.set('kind', kind)
    params.set('status', status)
    client
      .get<ApiResp<{ tokens: TokenRow[] }>>(`/tokens?${params.toString()}`, { headers: { Authorization: `Bearer ${getToken()}` } })
      .then((r) => setRows(r.data.data?.tokens ?? []))
      .catch((e) => message.error(errMsg(e)))
      .finally(() => setLoading(false))
  }

  useEffect(load, [kind, status])

  const revoke = async (jti: string, name: string) => {
    try {
      await client.post(`/tokens/${jti}/revoke`, {}, { headers: { Authorization: `Bearer ${getToken()}` } })
      message.success(`已吊销 ${name || jti} 的令牌`)
      load()
    } catch (e) {
      message.error(errMsg(e))
    }
  }

  const columns: ColumnsType<TokenRow> = [
    {
      title: '主体', dataIndex: 'subject_name', width: 160,
      render: (_, r) => (
        <Space direction="vertical" size={0}>
          <span>{r.subject_name || `${r.subject_kind}:${r.subject_id}`}</span>
          <Tag color={KIND_COLOR[r.subject_kind]} style={{ marginTop: 2 }}>{KIND_TEXT[r.subject_kind] ?? r.subject_kind}</Tag>
        </Space>
      ),
    },
    { title: '角色', dataIndex: 'role', width: 100, render: (v: string) => ROLE_TEXT[v] ?? v },
    { title: 'Token ID', dataIndex: 'jti', ellipsis: true, render: (v: string) => <span style={{ fontFamily: 'monospace' }}>{v.slice(0, 16)}…</span> },
    { title: '签发时间', dataIndex: 'issued_at', width: 150, render: fmt },
    {
      title: '过期时间', dataIndex: 'expires_at', width: 150,
      render: (v: number) => (v < Math.floor(Date.now() / 1000) ? <Tag color="default">已过期</Tag> : fmt(v)),
    },
    { title: '最近使用', dataIndex: 'last_used_at', width: 150, render: fmt },
    {
      title: '状态', dataIndex: 'revoked_at', width: 90,
      render: (v: number | null) => (v ? <Tag color="red">已吊销</Tag> : <Tag color="green">有效</Tag>),
    },
    {
      title: '操作', width: 100,
      render: (_, r) =>
        isAdmin && !r.revoked_at ? (
          <Popconfirm title={`吊销 ${r.subject_name || r.jti.slice(0, 8)} 的令牌？`} description="吊销后该令牌立即失效，需重新登录/登录获取" onConfirm={() => revoke(r.jti, r.subject_name)}>
            <Button size="small" danger>吊销</Button>
          </Popconfirm>
        ) : null,
    },
  ]

  return (
    <Card
      title="令牌管理"
      extra={
        <Space size={8}>
          <Radio.Group value={kind} onChange={(e) => setKind(e.target.value)}
            options={[{ value: '', label: '全部' }, { value: 'user', label: '后台用户' }, { value: 'device', label: '设备' }]}
            optionType="button" buttonStyle="solid" size="small"
          />
          <Radio.Group value={status} onChange={(e) => setStatus(e.target.value)}
            options={[{ value: 'active', label: '有效' }, { value: 'revoked', label: '已吊销' }, { value: 'all', label: '全部' }]}
            optionType="button" buttonStyle="solid" size="small"
          />
        </Space>
      }
    >
      <Table rowKey="jti" loading={loading} dataSource={rows} columns={columns} pagination={{ pageSize: 20 }} />
      <div style={{ marginTop: 8, color: '#999', fontSize: 12 }}>
        令牌在登录/注册时自动签发，吊销后立即失效；过期令牌由系统自动清理。{isAdmin ? '仅管理员可执行吊销。' : '您只有查看权限，吊销需管理员。'}
      </div>
    </Card>
  )
}