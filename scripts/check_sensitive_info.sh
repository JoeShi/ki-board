#!/bin/bash
# Check for sensitive information in user prompt before sending to LLM
# USER_PROMPT env var is set by Kiro's promptSubmit hook

INPUT="${USER_PROMPT}"

if [ -z "$INPUT" ]; then
  exit 0
fi

FINDINGS=""

# Check for email addresses
if echo "$INPUT" | grep -qEi '[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}'; then
  FINDINGS="${FINDINGS}\n⚠️ 检测到邮箱地址"
fi

# Check for phone numbers (Chinese mobile, landline with area code, international format)
if echo "$INPUT" | grep -qE '(\+86[-\s]?)?1[3-9][0-9]{9}'; then
  FINDINGS="${FINDINGS}\n⚠️ 检测到疑似电话号码"
elif echo "$INPUT" | grep -qE '\+[0-9]{1,3}[-\s][0-9]{6,12}'; then
  FINDINGS="${FINDINGS}\n⚠️ 检测到疑似电话号码"
elif echo "$INPUT" | grep -qE '0[0-9]{2,3}[-\s]?[0-9]{7,8}'; then
  FINDINGS="${FINDINGS}\n⚠️ 检测到疑似电话号码"
fi

# Check for API keys / tokens (common patterns)
if echo "$INPUT" | grep -qEi '(api[_-]?key|token|secret|password|passwd|credential)[[:space:]]*[=:][[:space:]]*['\''"]?[a-zA-Z0-9_\-\.]{8,}'; then
  FINDINGS="${FINDINGS}\n⚠️ 检测到疑似 API Key/Token"
fi

# Check for IP addresses (private or public)
if echo "$INPUT" | grep -oE '\b([0-9]{1,3}\.){3}[0-9]{1,3}\b' | grep -qv -e '^127\.' -e '^0\.'; then
  FINDINGS="${FINDINGS}\n⚠️ 检测到 IP 地址"
fi

# Check for Chinese ID card numbers (18 digits)
if echo "$INPUT" | grep -qE '[1-9][0-9]{5}(19|20)[0-9]{2}(0[1-9]|1[0-2])(0[1-9]|[12][0-9]|3[01])[0-9]{3}[0-9Xx]'; then
  FINDINGS="${FINDINGS}\n⚠️ 检测到疑似身份证号"
fi

# Check for credit card numbers (13-19 digits, possibly with separators)
if echo "$INPUT" | grep -oE '\b([0-9]{4}[-\s]?){3,4}[0-9]{1,4}\b' | grep -qE '[0-9]{13,19}|([0-9]{4}[-\s]){3}'; then
  FINDINGS="${FINDINGS}\n⚠️ 检测到疑似银行卡号"
fi

if [ -n "$FINDINGS" ]; then
  # Output to stderr - Kiro sends stderr to agent when exit code is non-zero
  echo "🚨 敏感信息检测警告 - 已阻断发送" >&2
  echo "========================" >&2
  echo -e "$FINDINGS" >&2
  echo "========================" >&2
  echo "请移除敏感信息后重新发送。" >&2
  # Non-zero exit code blocks the prompt submission
  exit 1
fi

# No sensitive info found - allow prompt to proceed
exit 0
