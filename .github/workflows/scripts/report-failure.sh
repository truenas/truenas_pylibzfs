#!/usr/bin/env bash

######################################################################
# Open or update a deduplicated GitHub issue when a scheduled or watch
# CI run fails.  These runs have no PR to annotate, so failures would
# otherwise be easy to miss.
#
# Requires:
#   - gh (preinstalled on GitHub-hosted runners)
#   - GH_TOKEN in the environment, with issues:write
#   - WORKFLOW_NAME and RUN_URL in the environment
######################################################################

set -euo pipefail

LABEL="ci-failure"
TITLE="Scheduled CI failure"
BRANCH_FILE=".github/zfs-branch"

if [ -f "$BRANCH_FILE" ]; then
  ZFS_BRANCH="$(cat "$BRANCH_FILE")"
else
  ZFS_BRANCH="unknown"
fi

# Make sure the dedupe label exists (no-op if it already does).
gh label create "$LABEL" --color b60205 \
  --description "Automated scheduled/watch CI failure" >/dev/null 2>&1 || true

# Reuse an existing open issue so repeated failures do not spam the tracker.
EXISTING="$(gh issue list --label "$LABEL" --state open \
  --json number --jq '.[0].number // empty')"

NOTE="Run failed: **${WORKFLOW_NAME}**
- Run: ${RUN_URL}
- Watched ZFS branch: \`${ZFS_BRANCH}\`"

if [ -n "$EXISTING" ]; then
  echo "Appending to existing issue #${EXISTING}"
  gh issue comment "$EXISTING" --body "$NOTE"
else
  echo "Opening a new ${LABEL} issue"
  gh issue create --label "$LABEL" --title "$TITLE" \
    --body "${NOTE}

This issue auto-closes on the next green scheduled run."
fi
