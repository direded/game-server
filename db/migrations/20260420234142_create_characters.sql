-- migrate:up
CREATE TABLE characters (
    id           bigserial    PRIMARY KEY,
    account_id   bigint       NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name         citext       NOT NULL UNIQUE,
    location_id  int          NOT NULL,
    created_at   timestamptz  NOT NULL DEFAULT now()
);
CREATE INDEX characters_account_id_idx ON characters(account_id);

-- migrate:down
DROP TABLE characters;
