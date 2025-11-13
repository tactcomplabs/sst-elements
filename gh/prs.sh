#!/bin/bash

# gh help formatting

if [[ "$1" -gt 0 ]]; then
    limit="--limit $1"
fi

# Uncomment to see the json
#gh pr list --state all $limit \
#   --json state,number,author,title,labels,createdAt,updatedAt,closedAt,headRefName

gh pr list --state all $limit \
   --json state,number,author,title,labels,createdAt,updatedAt,closedAt,headRefName \
   --jq '
        map([
            .number,
            .state,
	    .author.login,
            .title,
	    (.createdAt | split(".")[0] | strptime("%Y-%m-%dT%H:%M:%SZ") | strftime("%Y-%m-%d")),
	    (.updatedAt | split(".")[0] | strptime("%Y-%m-%dT%H:%M:%SZ") | strftime("%Y-%m-%d")),
	    (if .closedAt == null then "" else (.closedAt | split(".")[0] | strptime("%Y-%m-%dT%H:%M:%SZ") | strftime("%Y-%m-%d")) end),
            (.labels | map(.name) | join(",")),
            .headRefName
        ])[]
        | @csv'
