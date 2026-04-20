-- migrate:up
CREATE TABLE sessions (
    token         text          PRIMARY KEY,
    account_id    bigint        NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    created_at    timestamptz   NOT NULL DEFAULT now(),
    expires_at    timestamptz   NOT NULL,
    last_seen_at  timestamptz   NOT NULL DEFAULT now()
);
CREATE INDEX sessions_account_id_idx ON sessions(account_id);

-- migrate:down
DROP TABLE sessions;
