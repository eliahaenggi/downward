#! /usr/bin/env python3

import itertools
import os

from lab.environments import LocalEnvironment, BaselSlurmEnvironment

from downward.reports.compare import ComparativeReport

import common_setup
from common_setup import IssueConfig, IssueExperiment

ARCHIVE_PATH = "ai/downward/TODO"
DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.environ["DOWNWARD_REPO"]
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
REVISIONS = ["803da3c8a76263475a9493fc2221e8b786f7ea31", "8d6e50096ce7ecb14402abbd02b74f1ef07db903"]
BUILDS = ["release"]
CONFIG_NICKS = [
    ("astar-h2", ["--search", "astar(h2())"])
]
CONFIGS = [
    IssueConfig(
        config_nick,
        config,
        build_options=[build],
        driver_options=['--search-time-limit', '5m', "--build", build])
    for build in BUILDS
    for config_nick, config in CONFIG_NICKS
]

SUITE = common_setup.DEFAULT_OPTIMAL_SUITE
ENVIRONMENT = BaselSlurmEnvironment(
    partition="infai_2",
    email="elia.haenggi@stud.unibas.ch",
    memory_per_cpu="3947M",
    export=["PATH"],
    # setup='export PATH=/scicore/soft/apps/CMake/3.23.1-GCCcore-11.3.0/bin:/scicore/soft/apps/libarchive/3.6.1-GCCcore-11.3.0/bin:/scicore/soft/apps/cURL/7.83.0-GCCcore-11.3.0/bin:/scicore/soft/apps/Python/3.10.4-GCCcore-11.3.0/bin:/scicore/soft/apps/OpenSSL/1.1/bin:/scicore/soft/apps/XZ/5.2.5-GCCcore-11.3.0/bin:/scicore/soft/apps/SQLite/3.38.3-GCCcore-11.3.0/bin:/scicore/soft/apps/Tcl/8.6.12-GCCcore-11.3.0/bin:/scicore/soft/apps/ncurses/6.3-GCCcore-11.3.0/bin:/scicore/soft/apps/bzip2/1.0.8-GCCcore-11.3.0/bin:/scicore/soft/apps/binutils/2.38-GCCcore-11.3.0/bin:/scicore/soft/apps/GCCcore/11.3.0/bin:/infai/sieverss/repos/bin:/infai/sieverss/local:/export/soft/lua_lmod/centos7/lmod/lmod/libexec:/usr/local/bin:/usr/bin:/usr/local/sbin:/usr/sbin:$PATH\nexport LD_LIBRARY_PATH=/scicore/soft/apps/libarchive/3.6.1-GCCcore-11.3.0/lib:/scicore/soft/apps/cURL/7.83.0-GCCcore-11.3.0/lib:/scicore/soft/apps/Python/3.10.4-GCCcore-11.3.0/lib:/scicore/soft/apps/OpenSSL/1.1/lib:/scicore/soft/apps/libffi/3.4.2-GCCcore-11.3.0/lib64:/scicore/soft/apps/GMP/6.2.1-GCCcore-11.3.0/lib:/scicore/soft/apps/XZ/5.2.5-GCCcore-11.3.0/lib:/scicore/soft/apps/SQLite/3.38.3-GCCcore-11.3.0/lib:/scicore/soft/apps/Tcl/8.6.12-GCCcore-11.3.0/lib:/scicore/soft/apps/libreadline/8.1.2-GCCcore-11.3.0/lib:/scicore/soft/apps/ncurses/6.3-GCCcore-11.3.0/lib:/scicore/soft/apps/bzip2/1.0.8-GCCcore-11.3.0/lib:/scicore/soft/apps/binutils/2.38-GCCcore-11.3.0/lib:/scicore/soft/apps/zlib/1.2.12-GCCcore-11.3.0/lib:/scicore/soft/apps/GCCcore/11.3.0/lib64',
)
"""
If your experiments sometimes have GCLIBX errors, you can use the
"setup" parameter instead of the "export" parameter above for setting
environment variables which "load" the right modules. ("module load"
doesn't do anything else than setting environment variables.)
# paths obtained via:
# module purge
# module -q load Python/3.10.4-GCCcore-11.3.0
# module -q load GCC/11.3.0
# module -q load CMake/3.23.1-GCCcore-11.3.0
# echo $PATH
# echo $LD_LIBRARY_PATH
"""

def add_translator_preprocessor_time(run):
    if "total_time" in run and "search_time" in run:
        run["preprocessing_time"] = run["total_time"] - run["search_time"]
    return True

if common_setup.is_test_run():
    SUITE = IssueExperiment.DEFAULT_TEST_SUITE
    ENVIRONMENT = LocalEnvironment(processes=4)

exp = IssueExperiment(
    REPO_DIR,
    revisions=REVISIONS,
    configs=CONFIGS,
    environment=ENVIRONMENT,
)
exp.add_suite(BENCHMARKS_DIR, SUITE)

exp.add_parser(exp.EXITCODE_PARSER)
exp.add_parser(exp.TRANSLATOR_PARSER)
exp.add_parser(exp.SINGLE_SEARCH_PARSER)
exp.add_parser(exp.PLANNER_PARSER)

exp.add_step('build', exp.build)
exp.add_step('start', exp.start_runs)
exp.add_step('parse', exp.parse)
exp.add_fetcher(name='fetch', filter=add_translator_preprocessor_time)

rev = REVISIONS[0]
def make_comparison_tables():
    compared_configs = [
        (f'astar-pi-m', f'astar-h2', 'Diff'),
    ]
    report = ComparativeReport(
        compared_configs, attributes=exp.DEFAULT_TABLE_ATTRIBUTES)
    outfile = os.path.join(
        exp.eval_dir,
        f"{exp.name}-comparison.{report.output_format}")
    report(exp.eval_dir, outfile)

SCATTER_PLOT_PAIRS = [
    ('astar-h2', 'astar-h2', rev, rev, attribute)
    for attribute in ['total_time', 'memory', 'preprocessing_time']
]


exp.add_absolute_report_step(filter=add_translator_preprocessor_time)
exp.add_step("make-comparison-tables", make_comparison_tables)
exp.add_scatter_plot_step(relative=False, additional=SCATTER_PLOT_PAIRS)


exp.run_steps()