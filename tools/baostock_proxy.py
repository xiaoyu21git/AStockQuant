"""
baostock_proxy.py — 证券宝 HTTP CONNECT 代理支持 (本机 Clash 等代理出口)

背景:
  - baostock 客户端是原生 TCP 协议 (直连 public-api.baostock.com:10030),
    不经过 HTTP 层, http_proxy 环境变量对它无效。
  - 证券宝按出口 IP 限流, 本机直连会被限; 通过 HTTP CONNECT 隧道改出口 IP。
  - 本模块 monkey-patch baostock.util.socketutil 的两个 socket 创建点
    (SocketUtil.connect / get_default_socket), 登录前调用一次即可, 零新依赖。

用法:
  from tools.baostock_proxy import apply_baostock_proxy
  apply_baostock_proxy("http://127.0.0.1:7890")            # Clash 默认混合端口
  apply_baostock_proxy("http://user:pass@127.0.0.1:7890")  # 带 Basic 认证

注意: Clash 需配置规则将 public-api.baostock.com 走代理节点, 否则 CONNECT 后
      仍按 DIRECT 出站, 出口 IP 不变等于没代理。
"""

from __future__ import annotations

import base64
import socket as _socket
from urllib.parse import urlsplit

# CONNECT 握手超时 (秒)
_CONNECT_TIMEOUT_S = 15.0
# 数据阶段读超时 (秒): 证券宝掐断连接不再应答时 (境外/机房出口 IP 被黑洞的典型表现),
# baostock 原 socket 无超时会永久阻塞; 正常数据流每块毫秒级到达, 120s 足够宽松
DATA_TIMEOUT_S = 120.0
# 代理响应头最大长度 (字节), 防异常响应死循环
_MAX_RESPONSE_HEADER_BYTES = 64 * 1024


def _http_connect_tunnel(proxy_host: str, proxy_port: int,
                         user: str, password: str,
                         target_host: str, target_port: int) -> _socket.socket:
    """经 HTTP 代理建立到目标地址的 CONNECT 隧道, 返回可读写的裸 socket"""
    sock = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
    sock.settimeout(_CONNECT_TIMEOUT_S)
    sock.connect((proxy_host, proxy_port))

    auth_line = ""
    if user:
        token = base64.b64encode(f"{user}:{password or ''}".encode("utf-8")).decode("ascii")
        auth_line = f"Proxy-Authorization: Basic {token}\r\n"
    req = (f"CONNECT {target_host}:{target_port} HTTP/1.1\r\n"
           f"Host: {target_host}:{target_port}\r\n"
           f"{auth_line}"
           f"Proxy-Connection: Keep-Alive\r\n\r\n")
    sock.sendall(req.encode("ascii"))

    # 逐字节读到响应头结束: 只消费代理的响应, 不吞目标服务器随后发来的数据
    head = bytearray()
    while not head.endswith(b"\r\n\r\n"):
        if len(head) > _MAX_RESPONSE_HEADER_BYTES:
            sock.close()
            raise RuntimeError("代理响应头超长, 疑似目标不是 HTTP 代理")
        chunk = sock.recv(1)
        if not chunk:
            sock.close()
            raise RuntimeError("代理在 CONNECT 握手阶段断开连接")
        head.extend(chunk)

    status_line = head.split(b"\r\n", 1)[0].decode("utf-8", errors="replace")
    if " 200" not in status_line:
        sock.close()
        raise RuntimeError(f"CONNECT 被代理拒绝: {status_line}")

    sock.settimeout(DATA_TIMEOUT_S)  # 数据阶段: 掐断变可见错误, 不再永久阻塞
    return sock


def set_data_timeout(seconds: float = DATA_TIMEOUT_S) -> None:
    """给当前 baostock 会话 socket 加读超时。

    直连模式 (不走代理隧道) 也必须调用: 代理隧道自带 DATA_TIMEOUT_S, 但直连时
    baostock 用的是原始无超时 socket, 服务器掐断后会永久阻塞。
    bs.login() 成功后调用一次即可。"""
    import baostock.common.context as context
    sock = getattr(context, "default_socket", None)
    if sock is not None:
        sock.settimeout(seconds)


def apply_baostock_proxy(proxy_url: str) -> None:
    """monkey-patch baostock 的 socket 创建点, 使其经 HTTP CONNECT 代理连接。
    必须在 bs.login() 之前调用。"""
    try:
        parts = urlsplit(proxy_url)
        proxy_port = parts.port or 80
    except ValueError as e:
        raise RuntimeError(f"代理地址格式错误: {proxy_url} ({e})") from e
    if parts.scheme != "http" or not parts.hostname:
        raise RuntimeError(f"代理地址格式错误: {proxy_url} (期望 http://[user:pass@]host:port)")

    import baostock.common.contants as cons
    import baostock.common.context as context
    import baostock.util.socketutil as sutil

    proxy_host = parts.hostname
    user = parts.username or ""
    password = parts.password or ""

    def _new_tunnel_socket() -> _socket.socket:
        return _http_connect_tunnel(
            proxy_host, proxy_port, user, password,
            cons.BAOSTOCK_SERVER_IP, cons.BAOSTOCK_SERVER_PORT)

    # 创建点 1: 登录用 SocketUtil.connect
    # (原实现在 connect 失败后 setattr 未定义变量直接 NameError, 这里一并规避)
    def _proxy_connect(self) -> None:
        try:
            my_socket = _new_tunnel_socket()
        except Exception as e:
            print(f"证券宝代理连接失败: {e}")
            raise
        setattr(context, "default_socket", my_socket)

    # 创建点 2: get_default_socket (个别查询路径直建连接, 保持失败返回 None 的原契约)
    def _proxy_get_default_socket():
        try:
            return _new_tunnel_socket()
        except Exception:
            print("证券宝代理连接失败, 请稍后再试。")
            return None

    sutil.SocketUtil.connect = _proxy_connect
    sutil.get_default_socket = _proxy_get_default_socket
    print(f"[proxy] 证券宝连接将经 HTTP CONNECT 代理: {proxy_host}:{proxy_port}")
