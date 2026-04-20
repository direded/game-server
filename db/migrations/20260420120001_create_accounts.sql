-- migrate:up
CREATE TABLE accounts (
    id            bigserial     PRIMARY KEY,
    username      citext        NOT NULL UNIQUE,
    password_hash text          NOT NULL,
    email         citext        NULL,
    created_at    timestamptz   NOT NULL DEFAULT now()
);

-- migrate:down
DROP TABLE accounts;
