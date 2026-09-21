// 浏览器端人脸特征工具：生成随机 512 维 float32 特征并 base64 编码
// 用于演示：Mock 边缘盒上报的特征 + 录入端的特征需一致才能回查命中。
const DIM = 512

export function randomFeatureBase64(seed = 0): string {
  const bytes = new Uint8Array(DIM * 4)
  // 简易确定性 PRNG（srand/rand 风格），支持 seed 复现：同 seed => 同特征
  let s = seed || 1
  const rand = () => {
    s = (1103515245 * s + 12345) & 0x7fffffff
    return s / 0x7fffffff
  }
  const view = new DataView(bytes.buffer)
  for (let i = 0; i < DIM; i++) view.setFloat32(i * 4, rand() * 2 - 1, true)
  return btoa(String.fromCharCode(...bytes))
}

export function randomBytesBase64(len = 2048): string {
  const bytes = new Uint8Array(len)
  crypto.getRandomValues(bytes)
  return btoa(String.fromCharCode(...bytes))
}
