import { useEffect, useState } from 'react'
import {
  Button,
  Card,
  Col,
  Divider,
  Form,
  Input,
  InputNumber,
  Row,
  Switch,
  message,
} from 'antd'
import { fetchConfig, saveConfig } from '../api'
import type { EdgeBoxConfig } from '../types'

// 系统配置：部署参数全部在页面维护（保存原子写盘 + 热重载标记），无需手改 JSON
export default function ConfigTab({ onSaved }: { onSaved: () => void }) {
  const [form] = Form.useForm()
  const [loading, setLoading] = useState(false)
  const [saving, setSaving] = useState(false)

  useEffect(() => {
    setLoading(true)
    fetchConfig()
      .then((cfg) => form.setFieldsValue(cfg))
      .catch(() => message.error('配置获取失败'))
      .finally(() => setLoading(false))
  }, [form])

  const onFinish = async (values: Partial<EdgeBoxConfig>) => {
    setSaving(true)
    try {
      // 密码/PSK：留空表示不修改；填写则更新
      const payload: Partial<EdgeBoxConfig> = { ...values }
      if (!payload.device_psk) delete payload.device_psk
      if (!payload.web_password) delete payload.web_password
      await saveConfig(payload)
      message.success('配置已保存')
      onSaved()
    } catch (e) {
      message.error((e as Error).message || '保存失败')
    } finally {
      setSaving(false)
    }
  }

  return (
    <Card size="small" title="系统配置">
      <Form<Partial<EdgeBoxConfig>>
        form={form}
        layout="vertical"
        onFinish={onFinish}
        style={{ maxWidth: 900 }}
        disabled={loading}
      >
        <Divider titlePlacement="start" plain>设备与上报</Divider>
        <Row gutter={16}>
          <Col span={6}>
            <Form.Item label="设备 ID" name="device_id" rules={[{ required: true, message: '必填' }]}>
              <InputNumber style={{ width: '100%' }} min={0} step={1} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="设备名称" name="device_name">
              <Input placeholder="edge-box" />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="门店 ID" name="store_id">
              <InputNumber style={{ width: '100%' }} min={0} step={1} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="上报端点（后台 /api/v1）" name="report_endpoint" rules={[{ required: true, message: '必填' }]}>
              <Input placeholder="http://host:port/api/v1" />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="上报间隔（秒）" name="report_interval_s">
              <InputNumber style={{ width: '100%' }} min={1} step={1} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="在线刷新间隔（秒）" name="online_refresh_interval_s">
              <InputNumber style={{ width: '100%' }} min={1} step={1} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="配置轮询间隔（秒）" name="config_poll_s">
              <InputNumber style={{ width: '100%' }} min={1} step={1} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="记录保留天数" name="retention_days">
              <InputNumber style={{ width: '100%' }} min={1} step={1} />
            </Form.Item>
          </Col>
        </Row>

        <Divider titlePlacement="start" plain>识别参数</Divider>
        <Row gutter={16}>
          <Col span={6}>
            <Form.Item label="检测阈值 det_thresh" name="det_thresh" tooltip="人脸检测置信度，默认 0.5">
              <InputNumber style={{ width: '100%' }} min={0} max={1} step={0.01} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="1:N 识别阈值 recog_thresh" name="recog_thresh" tooltip="客流识别/历史回查，默认 0.40">
              <InputNumber style={{ width: '100%' }} min={0} max={1} step={0.01} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="1:1 核验阈值 verify_thresh" name="verify_thresh" tooltip="人证核验（录入端），默认 0.50">
              <InputNumber style={{ width: '100%' }} min={0} max={1} step={0.01} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="最大处理帧数（0=无限）" name="max_frames">
              <InputNumber style={{ width: '100%' }} min={0} step={1} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="活体检测" name="liveness_enabled" valuePropName="checked">
              <Switch />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="内部人员识别" name="staff_enabled" valuePropName="checked">
              <Switch />
            </Form.Item>
          </Col>
        </Row>

        <Divider titlePlacement="start" plain>Web 服务与安全</Divider>
        <Row gutter={16}>
          <Col span={6}>
            <Form.Item label="启用 Web 服务" name="web_enabled" valuePropName="checked" tooltip="关闭后本配置界面不可访问">
              <Switch />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="Web 端口" name="web_port">
              <InputNumber style={{ width: '100%' }} min={1} max={65535} step={1} />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="Web 账号" name="web_username">
              <Input />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="Web 密码（留空保持不变）" name="web_password">
              <Input.Password placeholder="********" />
            </Form.Item>
          </Col>
          <Col span={6}>
            <Form.Item label="设备 PSK（留空保持不变）" name="device_psk" tooltip="与后台 DEVICE_PSK 一致">
              <Input.Password placeholder="********" />
            </Form.Item>
          </Col>
        </Row>

        <Button type="primary" htmlType="submit" loading={saving}>
          保存配置
        </Button>
      </Form>
    </Card>
  )
}