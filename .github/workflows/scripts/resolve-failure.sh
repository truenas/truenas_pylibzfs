#!/usr/bin/env bash

######################################################################
# Close any open ci-failure issues after a green scheduled run, so the
# tracker reflects the current state without manual bookkeeping.
#
# Requires:
#   - gh (preinstalled on GitHub-hosted runners)
#   - GH_TOKEN in the environment, with issues:write
#   - WORKFLOW_NAME and RUN_URL in the environment
######################################################################

set -euo pipefail

LABEL="ci-failure"

OPEN_ISSUES="$(gh issue list --label "$LABEL" --state open \
  --json number --jq '.[].number')"

if [ -z "$OPEN_ISSUES" ]; then
  echo "No open ${LABEL} issues to close."
  exit 0
fi

for n in $OPEN_ISSUES; do
  echo "Closing issue #${n}"
  gh issue close "$n" \
    --comment "Resolved: **${WORKFLOW_NAME}** is green again. Run: ${RUN_URL}"
done
