#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  kiro_agent_start.sh <agent-name> [kiro-cli args...]

Examples:
  kiro_agent_start.sh planner chat --trust-all-tools
  kiro_agent_start.sh coder chat --trust-all-tools

This script is meant to run inside a Ghostty split.
It starts kiro-cli with the selected custom agent.
EOF
}

if [[ $# -lt 1 ]]; then
  usage
  exit 1
fi

agent_name="$1"
shift

exec kiro-cli --agent "$agent_name" "$@"
