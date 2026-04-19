-- migrate:up
CREATE TABLE dummy (
    id    SERIAL PRIMARY KEY,
    value TEXT NOT NULL
);

-- migrate:down
DROP TABLE IF EXISTS dummy;
