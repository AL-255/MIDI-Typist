"""Build provenance tests in disposable repositories, never the real checkout."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import shutil
from generate_build_identity import identity, generate


class BuildIdentityTests(unittest.TestCase):
    def test_cmake_build_refreshes_without_reconfigure(self):
        project=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'tools').mkdir()
            shutil.copyfile(project/'tools/generate_build_identity.py',root/'tools/generate_build_identity.py')
            shutil.copyfile(project/'cmake/build_identity.cmake',root/'identity.cmake')
            (root/'.gitignore').write_text('/build/\n')
            (root/'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.20)\nproject(identity C)\n'
                'include(identity.cmake)\nadd_executable(probe main.c)\n'
                'add_dependencies(probe mt_build_identity)\n')
            (root/'main.c').write_text(
                '#include <stdio.h>\nint main(void) { puts(MT_GIT_COMMIT " " MT_GIT_STATE); }\n')
            def run(*args):
                return subprocess.check_output(args,cwd=root,stderr=subprocess.STDOUT).decode().strip()
            run('git','init','-q')
            run('git','config','user.name','Build test')
            run('git','config','user.email','build@example.invalid')
            run('git','config','commit.gpgsign','false')
            run('git','add','.');run('git','commit','-qm','fixture')
            head=run('git','rev-parse','HEAD')
            run('cmake','-S','.','-B','build','-G','Ninja')
            run('cmake','--build','build')
            self.assertEqual(run(str(root/'build/probe')),head+' clean')
            (root/'untracked.txt').write_text('local edit')
            run('cmake','--build','build')
            self.assertEqual(run(str(root/'build/probe')),head+' dirty')
            run('git','add','.');run('git','commit','-qm','next')
            run('cmake','--build','build')
            self.assertEqual(run(str(root/'build/probe')),run('git','rev-parse','HEAD')+' clean')

    def test_git_and_incremental_generation(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            def git(*args):
                return subprocess.check_output(['git','-C',str(root),*args],
                    stderr=subprocess.DEVNULL).decode().strip()
            git('init','-q')
            git('config','user.name','Build test')
            git('config','user.email','build@example.invalid')
            git('config','commit.gpgsign','false')
            self.assertEqual(identity(root),('unknown','unknown'))  # unborn HEAD
            (root/'source.c').write_text('int example;\n')
            (root/'.gitignore').write_text('/build/\n')
            git('add','.');git('commit','-qm','fixture')
            head=git('rev-parse','HEAD')
            self.assertEqual(identity(root),(head,'clean'))
            output=root/'build/git_identity.h'
            generate(root,output)
            self.assertIn(head,output.read_text())
            os.utime(output,ns=(1_000_000_000,1_000_000_000))
            generate(root,output)
            self.assertEqual(output.stat().st_mtime_ns,1_000_000_000)
            (root/'source.c').write_text('int changed;\n')
            generate(root,output)
            self.assertIn('MT_GIT_STATE "dirty"',output.read_text())
            git('add','.');git('commit','-qm','changed fixture')
            generate(root,output)
            self.assertNotEqual(head,git('rev-parse','HEAD'))
            self.assertIn(git('rev-parse','HEAD'),output.read_text())
            self.assertIn('MT_GIT_STATE "clean"',output.read_text())
            (root/'untracked.c').write_text('int untracked;\n')
            self.assertEqual(identity(root)[1],'dirty')
            nested=root/'export';nested.mkdir()
            self.assertEqual(identity(nested),('unknown','unknown'))

    def test_no_git_export(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);generate(root,root/'git_identity.h')
            self.assertEqual(identity(root),('unknown','unknown'))
            self.assertIn('MT_GIT_COMMIT "unknown"',(root/'git_identity.h').read_text())


if __name__ == '__main__':
    unittest.main()
