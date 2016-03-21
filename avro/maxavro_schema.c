/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2016
 */

#include "maxavro.h"
#include <jansson.h>
#include <string.h>
#include <skygw_debug.h>

static const MAXAVRO_SCHEMA_FIELD types[MAXAVRO_TYPE_MAX] =
{
    {"int", NULL, MAXAVRO_TYPE_INT},
    {"long", NULL, MAXAVRO_TYPE_LONG},
    {"float", NULL, MAXAVRO_TYPE_FLOAT},
    {"double", NULL, MAXAVRO_TYPE_DOUBLE},
    {"bool", NULL, MAXAVRO_TYPE_BOOL},
    {"bytes", NULL, MAXAVRO_TYPE_BYTES},
    {"string", NULL, MAXAVRO_TYPE_STRING},
    {"enum", NULL, MAXAVRO_TYPE_ENUM},
    {"null", NULL, MAXAVRO_TYPE_NULL},
    {NULL, NULL, MAXAVRO_TYPE_UNKNOWN}
};

static enum maxavro_value_type string_to_type(const char *str)
{
    for (int i = 0; types[i].name; i++)
    {
        if (strcmp(str, types[i].name) == 0)
        {
            return types[i].type;
        }
    }
    return MAXAVRO_TYPE_UNKNOWN;
}

static const char* type_to_string(enum maxavro_value_type type)
{
    for (int i = 0; types[i].name; i++)
    {
        if (types[i].type == type)
        {
            return types[i].name;
        }
    }
    return "unknown type";
}

static enum maxavro_value_type unpack_to_type(json_t *object,
                                              MAXAVRO_SCHEMA_FIELD* field)
{
    enum maxavro_value_type rval = MAXAVRO_TYPE_UNKNOWN;
    json_t* type = NULL;

#ifdef SS_DEBUG
    char *js = json_dumps(object, JSON_PRESERVE_ORDER);
    printf("%s\n", js);
    free(js);
#endif

    if (json_is_object(object))
    {
        json_t *tmp = NULL;
        json_unpack(object, "{s:o}", "type", &tmp);
        type = tmp;
    }

    if (json_is_array(object))
    {
        json_t *tmp = json_array_get(object, 0);
        type = tmp;
    }

    if (type && json_is_string(type))
    {
        const char *value = json_string_value(type);
        rval = string_to_type(value);

        if (rval == MAXAVRO_TYPE_ENUM)
        {
            json_t *tmp = NULL;
            json_unpack(object, "{s:o}", "symbols", &tmp);
            ss_dassert(json_is_array(tmp));
            json_incref(tmp);
            field->extra = tmp;
        }
    }

    return rval;
}

/**
 * @brief Create an Avro schema from JSON
 * @param json JSON where the schema is created from
 * @return New schema or NULL if an error occurred
 */
MAXAVRO_SCHEMA* maxavro_schema_from_json(const char* json)
{
    MAXAVRO_SCHEMA* rval = malloc(sizeof(MAXAVRO_SCHEMA));

    if (rval)
    {
        json_error_t err;
        json_t *schema = json_loads(json, 0, &err);

        if (schema)
        {
            json_t *field_arr = NULL;
            json_unpack(schema, "{s:o}", "fields", &field_arr);
            size_t arr_size = json_array_size(field_arr);
            rval->fields = malloc(sizeof(MAXAVRO_SCHEMA_FIELD) * arr_size);
            rval->num_fields = arr_size;

            for (int i = 0; i < arr_size; i++)
            {
                json_t *object = json_array_get(field_arr, i);
                char *key;
                json_t *value_obj;

                json_unpack(object, "{s:s s:o}", "name", &key, "type", &value_obj);
                rval->fields[i].name = strdup(key);
                rval->fields[i].type = unpack_to_type(value_obj, &rval->fields[i]);
            }

            json_decref(schema);
        }
        else
        {
            printf("Failed to read JSON schema: %s\n", json);
        }
    }
    else
    {
        printf("Memory allocation failed.\n");
    }
    return rval;
}

static void maxavro_schema_field_free(MAXAVRO_SCHEMA_FIELD *field)
{
    if (field->type == MAXAVRO_TYPE_ENUM)
    {
        json_decref((json_t*)field->extra);
    }
}

void maxavro_schema_free(MAXAVRO_SCHEMA* schema)
{
    if (schema)
    {
        for (int i = 0; i < schema->num_fields; i++)
        {
            maxavro_schema_field_free(&schema->fields[i]);
        }
        free(schema->fields);
        free(schema);
    }
}
