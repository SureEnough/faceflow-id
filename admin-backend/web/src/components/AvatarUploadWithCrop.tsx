import { useState } from 'react'
import { Form, Upload, message } from 'antd'
import { UploadOutlined } from '@ant-design/icons'
import type { UploadFile } from 'antd/es/upload/interface'
import AvatarCropper from './AvatarCropper'

// 文件 -> dataURL（用于裁剪预览）
function fileToDataURL(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onload = () => resolve(String(reader.result))
    reader.onerror = reject
    reader.readAsDataURL(file)
  })
}

interface Props {
  value?: string // 当前头像 base64（无前缀，受控）
  onChange?: (base64: string) => void
  label?: string
  extra?: string
  required?: boolean
}

/** 头像上传 + 自定义裁剪（受控组件）：选图 → 裁剪 → onChange(base64) */
export default function AvatarUploadWithCrop({
  value, onChange, label = '头像（正脸照片）', extra, required,
}: Props) {
  const [fileList, setFileList] = useState<UploadFile[]>([])
  const [cropSrc, setCropSrc] = useState('')
  const [cropOpen, setCropOpen] = useState(false)

  const onCropConfirm = (base64: string, dataUrl: string) => {
    setFileList([{ uid: '-1', name: 'avatar.jpg', status: 'done', url: dataUrl }])
    setCropOpen(false)
    onChange?.(base64)
  }

  return (
    <Form.Item label={label} extra={extra} required={required}>
      <Upload
        listType="picture-card"
        maxCount={1}
        fileList={fileList}
        accept="image/jpeg,image/png,image/bmp"
        beforeUpload={async (file) => {
          try {
            const dataUrl = await fileToDataURL(file)
            setCropSrc(dataUrl)
            setCropOpen(true)
          } catch {
            message.error('图片读取失败')
          }
          return false
        }}
        onRemove={() => { setFileList([]); onChange?.('') }}
        onPreview={() => { if (cropSrc) setCropOpen(true) }}
      >
        {fileList.length === 0 && (
          <div><UploadOutlined /><div style={{ marginTop: 8 }}>上传头像</div></div>
        )}
      </Upload>
      {value && !fileList.length && (
        <div style={{ marginTop: 4 }}>
          {/* 兼容外部已有值（如后端返回的照片），展示提示 */}
          <span style={{ color: '#999', fontSize: 12 }}>已设置头像（重新上传可替换）</span>
        </div>
      )}
      <AvatarCropper
        src={cropSrc}
        open={cropOpen}
        aspect={1}
        outputSize={512}
        onCancel={() => setCropOpen(false)}
        onConfirm={onCropConfirm}
      />
    </Form.Item>
  )
}