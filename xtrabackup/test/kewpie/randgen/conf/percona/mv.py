import shutil
import os
for file_name in os.listdir('.'):
    if not file_name.endswith('.py'):
        new_file_name = file_name.replace('drizzle','percona')
        shutil.move(file_name, new_file_name)


